/*
 * Copyright 2014 Samsung Electronics. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrHairLinePathRenderer.h"

#include "GrPathRenderer.h"
#include "GrContext.h"
#include "GrDefaultGeoProcFactory.h"
#include "SkString.h"
#include "SkStrokeRec.h"
#include "SkTLazy.h"
#include "SkTraceEvent.h"
#include "GrPathUtils.h"
#include "GrGpu.h"
#include "SkGeometry.h"
#include "GrProcessor.h"
#include "batches/GrBatch.h"
#include "GrBufferAllocPool.h"

#include "GrBatchFlushState.h"
#include "GrBatchTest.h"
#include "GrCaps.h"
#include "GrIndexBuffer.h"
#include "GrPipelineBuilder.h"
#include "GrResourceProvider.h"
#include "GrVertexBuffer.h"
#include "SkStroke.h"
#include "SkTemplates.h"
#include "batches/GrVertexBatch.h"

////////////////////////////////////////////////////////////////////////////////
static const int MAX_LINE_POINTS_PER_CURVE = 2048;  // 16 x 2048 = 32K
static const int MAX_POINTS = 1 << 11; // 2048
static const SkScalar gMinCurveTol = 0.0001f;
int maxPts;
int prevMaxPts;

GrIndexBuffer* indexBuffer = NULL;

struct HairLineVertex {
    SkPoint fPos;
    GrColor fColor;
    SkScalar fCoverage;
};

static uint32_t quadraticPointCount(const SkPoint points[], SkScalar tol) {
    if (tol < gMinCurveTol) {
        tol = gMinCurveTol;
    }
    SkASSERT(tol > 0);

    SkScalar d = points[1].distanceToLineSegmentBetween(points[0], points[2]);

    if (d <= tol) {
        return 2;
    } else {
        int temp = SkScalarCeilToInt(SkScalarSqrt(SkScalarDiv(d, tol)));
        // See comment in GrPathUtils.cpp, we double that because each
        // segment has two end points
        int pow2 = GrNextPow2(temp) * 2;

        return SkTMin(pow2, MAX_LINE_POINTS_PER_CURVE);
    }
}

static uint32_t generateQuadraticPoints(const SkPoint& p0,
                                        const SkPoint& p1,
                                        const SkPoint& p2,
                                        SkScalar tolSqd,
                                        HairLineVertex** points,
                                        uint32_t pointsLeft,
                                        GrColor color, SkScalar coverage) {
    if (pointsLeft <= 2 ||
        (p1.distanceToLineSegmentBetweenSqd(p0, p2)) <= tolSqd) {
        (*points)[0].fPos = p0;
        (*points)[0].fColor = color;
        (*points)[0].fCoverage = coverage;
        (*points)[1].fPos = p2;
        (*points)[1].fColor = color;
        (*points)[1].fCoverage = coverage;
        *points += 2;
        return 2;
    }

    SkPoint q[] = {
        { SkScalarAve(p0.fX, p1.fX), SkScalarAve(p0.fY, p1.fY) },
        { SkScalarAve(p1.fX, p2.fX), SkScalarAve(p1.fY, p2.fY) },
    };
    SkPoint r = { SkScalarAve(q[0].fX, q[1].fX), SkScalarAve(q[0].fY, q[1].fY) };
    pointsLeft >>= 1;
    uint32_t a = generateQuadraticPoints(p0, q[0], r, tolSqd, points, pointsLeft, color, coverage);
    uint32_t b = generateQuadraticPoints(r, q[1], p2, tolSqd, points, pointsLeft, color, coverage);
    return a + b;
}

static uint32_t conicPointCount(const SkPoint points[], SkScalar weight,
                                const SkMatrix& matrix, SkScalar tol) {
    if (tol < gMinCurveTol) {
        tol = gMinCurveTol;
    }
    SkASSERT(tol > 0);

    SkAutoConicToQuads actq;
    int numPts = 0;
    SkPoint pts[3];

    const SkPoint* quads = actq.computeQuads(points, weight, tol);
    int numQuads = actq.countQuads();

    for (int i = 0; i < numQuads; i++) {
        matrix.mapPoints(pts, &quads[i*2], 3);
        numPts += quadraticPointCount(pts, tol);
    }

    return numPts;
}

static uint32_t generateConicPoints(const SkPoint& p0,
                                    const SkPoint& p1,
                                    const SkPoint& p2,
                                    SkScalar weight,
                                    const SkMatrix& matrix,
                                    SkScalar tolSqd,
                                    HairLineVertex** points,
                                    GrColor color, SkScalar coverage) {
    SkAutoConicToQuads actq;
    int num = 0;
    SkPoint pts[3];
    SkScalar tol = SkScalarSqrt(tolSqd);

    pts[0] = p0; pts[1] = p1, pts[2] = p2;
    const SkPoint* quads = actq.computeQuads(pts, weight, tol);
    int numQuads = actq.countQuads();

    for (int i = 0; i < numQuads; i++) {
        matrix.mapPoints(pts, &quads[i*2], 3);
        num += generateQuadraticPoints(pts[0], pts[1], pts[2], tolSqd,
                                       points,
                                       quadraticPointCount(pts, tol),
                                       color, coverage);
    }

    return num;
}

static uint32_t cubicPointCount(const SkPoint points[], SkScalar tol) {
    if (tol < gMinCurveTol) {
        tol = gMinCurveTol;
    }

    SkASSERT(tol > 0);

    SkScalar d = SkTMax(
        points[1].distanceToLineSegmentBetweenSqd(points[0], points[3]),
        points[2].distanceToLineSegmentBetweenSqd(points[0], points[3]));
    d = SkScalarSqrt(d);
    if (d <= tol) {
        return 2;
    } else {
        int temp = SkScalarCeilToInt(SkScalarSqrt(SkScalarDiv(d, tol)));
        // See comment in GrPathUtils.cpp, we double that because each
        // segment has two end points
        int pow2 = GrNextPow2(temp) * 2;
        if (pow2 < 2) {
            pow2 = 2;
        }
        return SkTMin(pow2, MAX_LINE_POINTS_PER_CURVE);
    }
}

static uint32_t generateCubicPoints(const SkPoint& p0,
                                    const SkPoint& p1,
                                    const SkPoint& p2,
                                    const SkPoint& p3,
                                    SkScalar tolSqd,
                                    HairLineVertex** points,
                                    uint32_t pointsLeft,
                                    GrColor color, SkScalar coverage) {
    if (pointsLeft <= 2 ||
        (p1.distanceToLineSegmentBetweenSqd(p0, p3) <= tolSqd &&
         p2.distanceToLineSegmentBetweenSqd(p0, p3) <= tolSqd)) {
            (*points)[0].fPos = p0;
            (*points)[0].fColor = color;
            (*points)[0].fCoverage = coverage;
            (*points)[1].fPos = p3;
            (*points)[1].fColor = color;
            (*points)[1].fCoverage = coverage;
            *points += 2;
            return 2;
    }

    SkPoint q[] = {
        { SkScalarAve(p0.fX, p1.fX), SkScalarAve(p0.fY, p1.fY) },
        { SkScalarAve(p1.fX, p2.fX), SkScalarAve(p1.fY, p2.fY) },
        { SkScalarAve(p2.fX, p3.fX), SkScalarAve(p2.fY, p3.fY) }
    };
    SkPoint r[] = {
        { SkScalarAve(q[0].fX, q[1].fX), SkScalarAve(q[0].fY, q[1].fY) },
        { SkScalarAve(q[1].fX, q[2].fX), SkScalarAve(q[1].fY, q[2].fY) }
    };
    SkPoint s = { SkScalarAve(r[0].fX, r[1].fX), SkScalarAve(r[0].fY, r[1].fY) };

    pointsLeft >>= 1;
    uint32_t a = generateCubicPoints(p0, q[0], r[0], s, tolSqd, points, pointsLeft, color, coverage);
    uint32_t b = generateCubicPoints(s, r[1], q[2], p3, tolSqd, points, pointsLeft, color, coverage);
    return a + b;
}

static int worstCasePointCount(const SkMatrix& matrix, const SkPath& path, SkScalar tol) {
    if (tol < gMinCurveTol) {
        tol = gMinCurveTol;
    }

    SkASSERT(tol > 0);

    int pointCount = 0;

    SkPath::Iter iter(path, false);
    SkPath::Verb verb;

    SkPoint pts[4];
    SkPoint unmapped[4];
    while ((verb = iter.next(unmapped)) != SkPath::kDone_Verb) {
        switch (verb) {
            case SkPath::kLine_Verb:
                pointCount += 2;
                break;
            case SkPath::kQuad_Verb:
                matrix.mapPoints(pts, unmapped, 3);
                pointCount += quadraticPointCount(pts, tol);
                break;
            case SkPath::kCubic_Verb:
                matrix.mapPoints(pts, unmapped, 4);
                pointCount += cubicPointCount(pts, tol);
                break;
            case SkPath::kConic_Verb:
                pointCount += conicPointCount(unmapped, iter.conicWeight(),
                                              matrix, tol);
            case SkPath::kMove_Verb:
            default:
                break;
        }
    }

    return pointCount;
}

static inline void fill_indices(uint16_t* indices, const int count) {
    for (int i = 0; i < count; i++) {
        indices[i] = i;
    }
}

////////////////////////////////////////////////////////////////////////////////
GrHairLinePathRenderer::~GrHairLinePathRenderer() { }

static GrIndexBuffer* getIndexBuffer(GrGpu* gpu) {
    static GrIndexBuffer* fIndexBuffer;
    if (fIndexBuffer == NULL) {
        fIndexBuffer = gpu->createIndexBuffer(MAX_POINTS * sizeof(uint16_t), false);
        if (NULL != fIndexBuffer) {
            // FIXME: Use lock/unlock when port to later version
            uint16_t* indices = (uint16_t*) fIndexBuffer->map();
            if (NULL != indices) {
                fill_indices(indices, MAX_POINTS);
                fIndexBuffer->unmap();
            } else {
                indices = (uint16_t*) sk_malloc_throw(sizeof(uint16_t) * MAX_POINTS);
                fill_indices(indices, MAX_POINTS);
                if (!fIndexBuffer->updateData(indices, MAX_POINTS * sizeof(uint16_t))) {
                    fIndexBuffer->unref();
                    fIndexBuffer = NULL;
                }
                sk_free(indices);
            }
        }
    }
    return fIndexBuffer;
}

class HairlineBatch : public GrVertexBatch {
public:
    DEFINE_BATCH_CLASS_ID

    struct Geometry {
        GrColor fColor;
        uint8_t fCoverage;
        SkMatrix fViewMatrix;
        SkPath fPath;
        SkIRect fDevClipBounds;
        SkScalar fTolerance;
        SkScalar fSrcSpaceTolSqd;
    };

    static GrDrawBatch* Create(const Geometry& geometry) { return new HairlineBatch(geometry); }

    const char* name() const override { return "HairlineBatch"; }

    void getInvariantOutputColor(GrInitInvariantOutput* out) const override {
        // When this is called on a batch, there is only one geometry bundle
        out->setKnownFourComponents(fGeoData[0].fColor);
    }
    void getInvariantOutputCoverage(GrInitInvariantOutput* out) const override {
        out->setUnknownSingleComponent();
    }

private:
    void initBatchTracker(const GrPipelineOptimizations& opt) override {
        // Handle any color overrides
        if (!opt.readsColor()) {
            fGeoData[0].fColor = GrColor_ILLEGAL;
        }
        opt.getOverrideColorIfSet(&fGeoData[0].fColor);

        // setup batch properties
        fBatch.fColorIgnored = !opt.readsColor();
        fBatch.fColor = fGeoData[0].fColor;
        fBatch.fUsesLocalCoords = opt.readsLocalCoords();
        fBatch.fCoverageIgnored = !opt.readsCoverage();
        fBatch.fCoverage = fGeoData[0].fCoverage;
    }

    SkSTArray<1, Geometry, true>* geoData() { return &fGeoData; }

    void onPrepareDraws(Target*) override;

    typedef SkTArray<SkPoint, true> PtArray;
    typedef SkTArray<int, true> IntArray;
    typedef SkTArray<float, true> FloatArray;

    HairlineBatch(const Geometry& geometry) : INHERITED(ClassID()) {
        fGeoData.push_back(geometry);

        // compute bounds
        fBounds = geometry.fPath.getBounds();
        geometry.fViewMatrix.mapRect(&fBounds);

        // This is b.c. hairlines are notionally infinitely thin so without expansion
        // two overlapping lines could be reordered even though they hit the same pixels.
        fBounds.outset(0.5f, 0.5f);
    }

    bool onCombineIfPossible(GrBatch* t, const GrCaps& caps) override {
        HairlineBatch* that = t->cast<HairlineBatch>();

        if (!GrPipeline::CanCombine(*this->pipeline(), this->bounds(), *that->pipeline(),
                                    that->bounds(), caps)) {
            return false;
        }

        if (this->viewMatrix().hasPerspective() != that->viewMatrix().hasPerspective()) {
            return false;
        }

        // We go to identity if we don't have perspective
        if (this->viewMatrix().hasPerspective() &&
            !this->viewMatrix().cheapEqualTo(that->viewMatrix())) {
            return false;
        }

        // TODO we can actually batch hairlines if they are the same color in a kind of bulk method
        // but we haven't implemented this yet
        // TODO investigate going to vertex color and coverage?
        if (this->coverage() != that->coverage()) {
            return false;
        }

        if (this->color() != that->color()) {
            //return false; // We are intended to batch hairlines of different colors.
        }

        SkASSERT(this->usesLocalCoords() == that->usesLocalCoords());
        if (this->usesLocalCoords() && !this->viewMatrix().cheapEqualTo(that->viewMatrix())) {
            return false;
        }

        fGeoData.push_back_n(that->geoData()->count(), that->geoData()->begin());
        this->joinBounds(that->bounds());
        return true;
    }

    GrColor color() const { return fBatch.fColor; }
    uint8_t coverage() const { return fBatch.fCoverage; }
    bool usesLocalCoords() const { return fBatch.fUsesLocalCoords; }
    const SkMatrix& viewMatrix() const { return fGeoData[0].fViewMatrix; }
    bool coverageIgnored() const { return fBatch.fCoverageIgnored; }

    struct BatchTracker {
        GrColor fColor;
        uint8_t fCoverage;
        SkRect fDevBounds;
        bool fUsesLocalCoords;
        bool fColorIgnored;
        bool fCoverageIgnored;
    };

    BatchTracker fBatch;
    SkSTArray<1, Geometry, true> fGeoData;

    typedef GrVertexBatch INHERITED;
};

void HairlineBatch::onPrepareDraws(Target* target) {
    // Setup the viewmatrix and localmatrix for the GrGeometryProcessor.
    SkMatrix invert;
    if (!this->viewMatrix().invert(&invert)) {
        return;
    }

     // we will transform to identity space if the viewmatrix does not have perspective
     bool hasPerspective = this->viewMatrix().hasPerspective();
     const SkMatrix* geometryProcessorViewM = &SkMatrix::I();
     const SkMatrix* geometryProcessorLocalM = &invert;
     if (hasPerspective) {
         geometryProcessorViewM = &this->viewMatrix();
         geometryProcessorLocalM = &SkMatrix::I();
     }


    SkAutoTUnref<const GrGeometryProcessor> lineGP;
    {
        using namespace GrDefaultGeoProcFactory;

        Color color(Color::kAttribute_Type);
        //color.fType = Color::kAttribute_Type;
        Coverage coverage(Coverage::kAttribute_Type);
        coverage.fCoverage = this->coverage();
        LocalCoords localCoords(this->usesLocalCoords() ? LocalCoords::kUsePosition_Type :
                                                          LocalCoords::kUnused_Type);
        localCoords.fMatrix = geometryProcessorLocalM;
        lineGP.reset(GrDefaultGeoProcFactory::Create(color, coverage, localCoords,
                                                     *geometryProcessorViewM));
    }
    target->initDraw(lineGP, this->pipeline());
    int instanceCount = fGeoData.count();
    const GrVertexBuffer* vertexBuffer;
    int firstVertex;
    size_t vertexStride = lineGP->getVertexStride();
    if (maxPts == 0)
        maxPts = prevMaxPts;
#if 1
    HairLineVertex* verts = reinterpret_cast<HairLineVertex*>(
            target->makeVertexSpace(vertexStride, maxPts, &vertexBuffer, &firstVertex));
#else
    InstancedHelper helper;
    HairLineVertex *verts = NULL;
    verts = reinterpret_cast<HairLineVertex*>(helper.init(target,
            kLines_GrPrimitiveType, vertexStride, indexBuffer, 2, 2, maxPts/2));
#endif

    if (!verts || !indexBuffer || !maxPts) {
        SkDebugf("Could not allocate vertices\n");
        maxPts = 0;
        return;
    }

    HairLineVertex* base = verts;
    Geometry args;
    for (int i = 0; i < instanceCount; i++) {
        args = fGeoData[i];

        SkPoint pts[4];
        SkPoint unmapped[4];
        SkPath::Iter iter(args.fPath, false);
        for (;;) {
            SkPath::Verb verb = iter.next(unmapped);
            switch (verb) {
                // FIXME:  This is not efficient.  We are doing this again
                // during tessellation
                case SkPath::kConic_Verb:
                    generateConicPoints(unmapped[0], unmapped[1], unmapped[2],
                                        iter.conicWeight(), args.fViewMatrix,
                                        args.fSrcSpaceTolSqd, &verts,
                                        args.fColor, args.fCoverage);
                    break;
                case SkPath::kLine_Verb:
                    args.fViewMatrix.mapPoints(pts, unmapped, 2);
                    (*verts).fPos = pts[0];
                    (*verts).fColor = args.fColor;
                    (*verts).fCoverage = args.fCoverage;
                    verts++;
                    (*verts).fPos = pts[1];
                    (*verts).fColor = args.fColor;
                    (*verts).fCoverage = args.fCoverage;
                    verts++;
                    break;
                case SkPath::kQuad_Verb:
                    args.fViewMatrix.mapPoints(pts, unmapped, 3);
                    generateQuadraticPoints(pts[0], pts[1], pts[2],
                                            args.fSrcSpaceTolSqd, &verts,
                                            quadraticPointCount(pts, args.fTolerance),
                                            args.fColor, args.fCoverage);
                    break;
                case SkPath::kCubic_Verb:
                    args.fViewMatrix.mapPoints(pts, unmapped, 4);
                    generateCubicPoints(pts[0], pts[1], pts[2], pts[3],
                                        args.fSrcSpaceTolSqd, &verts,
                                        cubicPointCount(pts, args.fTolerance),
                                        args.fColor, args.fCoverage);
                    break;
                case SkPath::kClose_Verb:
                case SkPath::kMove_Verb:
                    break;
                case SkPath::kDone_Verb:
                    goto BREAKLOOP;
            }
        }
BREAKLOOP:
    {}
    }
    {
        int vertex = static_cast<int>(verts - base);
#if 1
        GrVertices vertices;
        vertices.initInstanced(kLines_GrPrimitiveType, vertexBuffer, indexBuffer,
                               firstVertex, 2, 2, (vertex/2),
                               MAX_POINTS/2);
        target->draw(vertices);
#else
        helper.recordDraw(target);
#endif
    }
    prevMaxPts = maxPts;
    maxPts=0;
}

static GrDrawBatch* create_hairline_batch(GrColor color,
                                          const SkMatrix& viewMatrix,
                                          const SkPath& path,
                                          const GrStrokeInfo& stroke,
                                          const SkIRect& devClipBounds) {
    SkScalar hairlineCoverage;

    GrPathRenderer::IsStrokeHairlineOrEquivalent(stroke, viewMatrix, &hairlineCoverage);

    SkScalar tolerance;
    tolerance = SK_Scalar1;
    tolerance = GrPathUtils::scaleToleranceToSrc(tolerance,
                                                 viewMatrix,
                                                 path.getBounds());
    SkScalar srcSpaceTolSqd = SkScalarMul(tolerance, tolerance);

    HairlineBatch::Geometry geometry;
    geometry.fColor = color;
    geometry.fCoverage = SkScalarRoundToInt(hairlineCoverage);
    geometry.fViewMatrix = viewMatrix;
    geometry.fPath = path;
    geometry.fDevClipBounds = devClipBounds;
    geometry.fTolerance = tolerance;
    geometry.fSrcSpaceTolSqd = srcSpaceTolSqd;

    return HairlineBatch::Create(geometry);
}

bool GrHairLinePathRenderer::canDrawPath(const GrDrawTarget* target,
                                         const GrPipelineBuilder* pipelineBuilder,
                                         const SkMatrix& viewMatrix,
                                         const SkPath& path,
                                         const GrStrokeInfo& stroke,
                                         bool antiAlias) const {
    // This class can draw any path with any fill but doesn't do any anti-aliasing.
    bool canDraw = !antiAlias &&
        (stroke.isHairlineStyle() ||
         IsStrokeHairlineOrEquivalent(stroke, viewMatrix, NULL));

    if (canDraw) {
        SkMatrix viewM = viewMatrix;
        SkScalar tol = SK_Scalar1;
        tol = GrPathUtils::scaleToleranceToSrc(tol,
                                               viewMatrix,
                                               path.getBounds());
        int maxPts = worstCasePointCount(viewM, path, tol);
        ((GrHairLinePathRenderer*)this)->setNumberOfPts(maxPts);
        return maxPts > 0 && maxPts <= MAX_POINTS;
    }
    return false;
}

bool GrHairLinePathRenderer::onDrawPath(GrDrawTarget* drawTarget,
                                        GrPipelineBuilder* pipelineBuilder,
                                        GrColor color,
                                        const SkMatrix& viewMatrix,
                                        const SkPath& path,
                                        const GrStrokeInfo& stroke,
                                        bool stencilOnly) {
    SkIRect devClipBounds;
    pipelineBuilder->clip().getConservativeBounds(pipelineBuilder->getRenderTarget(),
                                                  &devClipBounds);

    maxPts += fNumPts;
    if (!maxPts)
        return false;

    GrContext* context = pipelineBuilder->getRenderTarget()->getContext();
    indexBuffer = getIndexBuffer(context->getGpu());
    if (NULL == indexBuffer) {
        fNumPts = 0;
        return false;
    }

    SkAutoTUnref<GrDrawBatch> batch(create_hairline_batch(color, viewMatrix, path,
                                                          stroke, devClipBounds));
    drawTarget->drawBatch(*pipelineBuilder, batch);

    return true;


}
