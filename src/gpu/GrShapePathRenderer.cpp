/*
 * Copyright 2014 Samsung Research America, Inc. - SiliconValley
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrShapePathRenderer.h"
#include <stdio.h>
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


GrShapePathRenderer::GrShapePathRenderer() {}

////////////////////////////////////////////////////////////////////////////////
// Stencil rules for paths
GR_STATIC_CONST_SAME_STENCIL(gShapeStencilOverWrite,
                             kZero_StencilOp,
                             kZero_StencilOp,
                             kEqual_StencilFunc,
                             0xffff,
                             0xffff,
                             0xffff);

GR_STATIC_CONST_SAME_STENCIL(gShapeStencilKeep,
                             kKeep_StencilOp,
                             kKeep_StencilOp,
                             kEqual_StencilFunc,
                             0xffff,
                             0xffff,
                             0xffff);

////////////////////////////////////////////////////////////////////////////////
// Helpers for drawPath

#define STENCIL_OFF     0   // Always disable stencil (even when needed)

GrPathRenderer::StencilSupport GrShapePathRenderer::onGetStencilSupport(
                                                   const SkPath&,
                                                   const GrStrokeInfo&) const {
    return GrPathRenderer::kNoRestriction_StencilSupport;
}

class ShapeBatch : public GrVertexBatch {
public:
    DEFINE_BATCH_CLASS_ID

    struct Geometry {
        GrColor fColor;
        uint8_t fCoverage;
        SkMatrix fViewMatrix;
        SkPath fInnerPath;
        SkPath fOuterPath;
        SkPath fCapsJoinsPath;
        SkRect fDevClipBounds;
        SkScalar fTolerance;
        SkScalar fSrcSpaceTolSqd;
    };

    static GrDrawBatch* Create(const Geometry& geometry) { return new ShapeBatch(geometry); }

    const char* name() const override { return "ShapeBatch"; }

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

    ShapeBatch(const Geometry& geometry) : INHERITED(ClassID()) {
        fGeoData.push_back(geometry);
        this->setBounds (geometry.fDevClipBounds);
    }

    bool onCombineIfPossible(GrBatch* t, const GrCaps& caps) override {
        return false; // Dont batch. 
        ShapeBatch* that = t->cast<ShapeBatch>();

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
            return false; // We are intended to batch hairlines of different colors.
        }

        SkASSERT(this->usesLocalCoords() == that->usesLocalCoords());
        if (this->usesLocalCoords() && !this->viewMatrix().cheapEqualTo(that->viewMatrix())) {
            return false;
        }

        fGeoData.push_back_n(that->geoData()->count(), that->geoData()->begin());
        this->joinBounds(that->bounds());
        return true;
    }

bool createGeom(void *vertices, const SkPath& outer,
                                const SkPath& inner,
                                const SkPath& joinsAndCaps,
                                SkScalar srcSpaceTol,
                                const int maxPts,
                                GrPrimitiveType* primType,
                                int* vertexCnt) {

    SkScalar srcSpaceTolSqd = SkScalarMul(srcSpaceTol, srcSpaceTol);

    *primType = kTriangles_GrPrimitiveType;


    SkPoint* base = reinterpret_cast<SkPoint*>(vertices);
    SkASSERT(NULL != base);
    SkPoint* vert = base;

    SkPoint outerPts[4];
    SkPoint innerPts[4];
    SkPoint pts[4];

    // Tessellate outer and inner path,
    // inner and outer path should have same verbs
    SkPath::Iter outerIter(outer, false);
    SkPath::Iter innerIter(inner, false);

    for (;;) {
        SkPath::Verb outerVerb = outerIter.next(outerPts);
        innerIter.next(innerPts);
        switch (outerVerb) {
            case SkPath::kConic_Verb:
                SkASSERT(0);
                break;
            case SkPath::kMove_Verb:
                break;
            case SkPath::kLine_Verb:
                *(vert++) = outerPts[0];
                *(vert++) = outerPts[1];
                *(vert++) = innerPts[1];
                *(vert++) = outerPts[0];
                *(vert++) = innerPts[0];
                *(vert++) = innerPts[1];
                break;
            case SkPath::kQuad_Verb:
                // first pt of quad is the pt we ended on in previous step
                GrPathUtils::generateShapedQuadraticPoints(outerPts, innerPts,
                            srcSpaceTolSqd, &vert,
                            GrPathUtils::quadraticPointCount(outerPts, srcSpaceTol),
                            GrPathUtils::quadraticPointCount(innerPts, srcSpaceTol));
                break;
            case SkPath::kCubic_Verb:
                // First pt of cubic is the pt we ended on in previous step
                GrPathUtils::generateShapedCubicPoints(outerPts, innerPts,
                                srcSpaceTolSqd, &vert,
                                GrPathUtils::cubicPointCount(outerPts, srcSpaceTol),
                                GrPathUtils::cubicPointCount(innerPts, srcSpaceTol));
                break;
            case SkPath::kClose_Verb:
                break;
            case SkPath::kDone_Verb:
                goto CAPS_AND_JOINS;
        }
    }

CAPS_AND_JOINS:
    // Tessellate joins and caps
    SkPath::Iter iter(joinsAndCaps, false);
    SkPoint lastPt;

    for (;;) {
        SkPath::Verb verb = iter.next(pts);
        switch (verb) {
            case SkPath::kConic_Verb:
                SkASSERT(0);
                break;
            case SkPath::kMove_Verb:
                lastPt = pts[0];
                break;
            case SkPath::kLine_Verb:
                if (lastPt != pts[0 || lastPt!= pts[1]]) {
                    *(vert++) = lastPt;
                    *(vert++) = pts[0];
                    *(vert++) = pts[1];
                }
                break;
            case SkPath::kQuad_Verb:
                // First pt of quad is the pt we ended on in previous step
                GrPathUtils::generateFanQuadraticPoints(
                            pts[0], pts[1], pts[2], lastPt,
                            srcSpaceTolSqd, &vert,
                            GrPathUtils::quadraticPointCount(pts, srcSpaceTol));
                break;
            case SkPath::kCubic_Verb:
                break;
            case SkPath::kClose_Verb:
                break;
            case SkPath::kDone_Verb:
                // uint16_t currIdx = (uint16_t) (vert - base);
                goto FINISHED;
        }
    }
FINISHED:
    SkASSERT((vert - base) <= maxPts);

    *vertexCnt = static_cast<int>(vert - base);

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

void ShapeBatch::onPrepareDraws(Target* target) {
    // Setup the viewmatrix and localmatrix for the GrGeometryProcessor.
    SkMatrix invert;
    if (!this->viewMatrix().invert(&invert)) {
        return;
    }

    SkAutoTUnref<const GrGeometryProcessor> gp;
    {
        using namespace GrDefaultGeoProcFactory;
        Color color(this->color());
        Coverage coverage(this->coverage());
        if (this->coverageIgnored()) {
            coverage.fType = Coverage::kNone_Type;
        }
        LocalCoords localCoords(this->usesLocalCoords() ? LocalCoords::kUsePosition_Type :
                                                          LocalCoords::kUnused_Type);
        gp.reset(GrDefaultGeoProcFactory::Create(color, coverage, localCoords,
                                                 this->viewMatrix()));
    }

    size_t vertexStride = gp->getVertexStride();
    SkASSERT(vertexStride == sizeof(SkPoint));

    target->initDraw(gp, this->pipeline());

    int instanceCount = fGeoData.count();
    int maxPts = 0;

    for (int i = 0; i < instanceCount; i++) {
        Geometry& args = fGeoData[i];
        int outerContourCnt;
        int innerContourCnt;
        int joinsContourCnt;
        int contourCnt;
        maxPts += GrPathUtils::worstCasePointCount(args.fOuterPath, &outerContourCnt,
                                                   args.fSrcSpaceTolSqd) * 6;
        contourCnt = outerContourCnt;
        maxPts += GrPathUtils::worstCasePointCount(args.fInnerPath, &innerContourCnt,
                                                   args.fSrcSpaceTolSqd) * 6;
        contourCnt += innerContourCnt;
        maxPts += GrPathUtils::worstCasePointCount(args.fCapsJoinsPath, &joinsContourCnt,
                                                   args.fSrcSpaceTolSqd) * 6;
        contourCnt += joinsContourCnt;

        if (maxPts <= 0) {
            return;
        }
        if (maxPts > ((int)SK_MaxU16 + 1)) {
            // GrPrintf("Path not rendered, too many verts (%d)\n", maxPts);
            return;
        }
    }

    // allocate vertex / index buffers
    const GrVertexBuffer* vertexBuffer;
    int firstVertex;

    void* verts = target->makeVertexSpace(vertexStride, maxPts,
                                          &vertexBuffer, &firstVertex);

    if (!verts) {
        SkDebugf("Could not allocate vertices\n");
        return;
    }

    int vertexOffset = 0;
    for (int i = 0; i < instanceCount; i++) {
        Geometry& args = fGeoData[i];
	    int vertexCnt;
        GrPrimitiveType primType;
        if (!createGeom(verts, args.fOuterPath,
                        args.fInnerPath,
                        args.fCapsJoinsPath,
                        args.fTolerance,
                        maxPts,
                        &primType,
                        &vertexCnt)) {
            return;
        }
        vertexOffset += vertexCnt;
	}

    GrVertices vertices;
    vertices.init(kTriangles_GrPrimitiveType, vertexBuffer, firstVertex, vertexOffset);
    target->draw(vertices);

    // put back reserves
    target->putBackVertices((size_t)(maxPts - vertexOffset), (size_t)vertexStride);
}

static GrDrawBatch* create_shape_batch(GrColor color,
                                          const SkMatrix& viewMatrix,
                                          const SkPath& outer,
                                          const SkPath& inner,
                                          const SkPath& join,
                                          const GrStrokeInfo& stroke,
                                          const SkRect& devClipBounds) {
    SkMatrix viewM = viewMatrix;
    uint8_t newCoverage = 0xff;

    SkScalar tol = SK_Scalar1;
    tol = GrPathUtils::scaleToleranceToSrc(tol, viewM, outer.getBounds());
    SkScalar srcSpaceTolSqd = SkScalarMul(tol, tol);


    ShapeBatch::Geometry geometry;
    geometry.fColor = color;
    geometry.fCoverage = newCoverage;
    geometry.fViewMatrix = viewMatrix;
    geometry.fOuterPath = outer;
    geometry.fInnerPath = inner;
    geometry.fCapsJoinsPath = join;
    geometry.fDevClipBounds = devClipBounds;
    geometry.fTolerance = tol;
    geometry.fSrcSpaceTolSqd = srcSpaceTolSqd;

    return ShapeBatch::Create(geometry);
}

bool GrShapePathRenderer::internalDrawPath(const SkPath& outer,
                                           const SkPath& inner,
                                           const SkPath& join,
                                           const GrStrokeInfo& origStroke,
                                           GrDrawTarget* target,
                                           GrPipelineBuilder* pipelineBuilder,
                                           GrColor color,
                                           const SkMatrix& viewMatrix,
                                           bool isOpaque) {


    SkMatrix viewM = viewMatrix;

    SkASSERT(NULL != target);
    // Face culling doesn't make sense here
    SkASSERT(GrPipelineBuilder::kBoth_DrawFace == pipelineBuilder->getDrawFace());


    GrStencilSettings stencilSetting;
    GrPipelineBuilder::DrawFace drawFace =  GrPipelineBuilder::kBoth_DrawFace;


    SkRect devBounds;
    GetPathDevBounds(outer, pipelineBuilder->getRenderTarget(), viewM, &devBounds);
    SkRect capsAndJoinsBounds, innerBounds;
    GetPathDevBounds(join, pipelineBuilder->getRenderTarget(), viewM, &capsAndJoinsBounds);
    GetPathDevBounds(inner, pipelineBuilder->getRenderTarget(), viewM, &innerBounds);
    devBounds.join(capsAndJoinsBounds.left(), capsAndJoinsBounds.top(),
                   capsAndJoinsBounds.right(), capsAndJoinsBounds.bottom());
    devBounds.join(innerBounds.left(), innerBounds.top(),
                   innerBounds.right(), innerBounds.bottom());

    pipelineBuilder->setDrawFace(drawFace);

    if (!pipelineBuilder->isOpaque()) {
        stencilSetting = (GrStencilSettings) gShapeStencilOverWrite;
		pipelineBuilder->setStencilBufferForWindingRules(false);
		pipelineBuilder->setClipBitsOverWrite(true);
    } else {
        stencilSetting = (GrStencilSettings) gShapeStencilKeep;
		pipelineBuilder->setStencilBufferForWindingRules(false);
		pipelineBuilder->setClipBitsOverWrite(false);
    }

    stencilSetting.setOverWrite();
    *pipelineBuilder->stencil() = stencilSetting;
    SkAutoTUnref<GrDrawBatch> batch(create_shape_batch(color, viewMatrix, outer, inner, join,
                                                           origStroke, devBounds));

    target->drawBatch(*pipelineBuilder, batch);
    return true;
}

bool GrShapePathRenderer::canDrawPath(const SkPath& pathA,
                                      const SkPath& pathB,
                                      const SkPath& pathC,
                                      const GrStrokeInfo& stroke,
                                      const GrDrawTarget* target,
                                      GrPipelineBuilder* pipelineBuilder,
                                      GrColor color,
                                      const SkMatrix& viewMatrix,
                                      bool antiAlias) const {
    // This class can draw any path with any fill but doesn't do any anti-aliasing.
    return !antiAlias &&
         SkStrokeRec::kStroke_Style == stroke.getStyle() &&
         !IsStrokeHairlineOrEquivalent(stroke, viewMatrix, NULL);
}

bool onDrawPath(GrDrawTarget*,
                            GrPipelineBuilder*,
                            GrColor,
                            const SkMatrix& viewMatrix,
                            const SkPath&,
                            const SkStrokeRec&,
                            bool antiAlias) {
    return false;
}

bool GrShapePathRenderer::onDrawPath(const SkPath& outer,
                                     const SkPath& inner,
                                     const SkPath& join,
                                     const GrStrokeInfo& stroke,
                                     GrDrawTarget* target,
                                     GrPipelineBuilder* pipelineBuilder,
                                     GrColor color,
                                     const SkMatrix& viewMatrix,
                                     bool isOpaque) {
    return this->internalDrawPath(outer,
                                  inner,
                                  join,
                                  stroke,
                                  target,
                                  pipelineBuilder,
                                  color,
                                  viewMatrix,
                                  isOpaque);
}

void GrShapePathRenderer::onStencilPath(const SkPath& outer,
                                        const SkPath& inner,
                                        const SkPath& join,
                                        const GrStrokeInfo& stroke,
                                        GrDrawTarget* target,
                                        GrPipelineBuilder* pipelineBuilder,
                                        GrColor color,
                                        const SkMatrix& viewMatrix) {
    this->internalDrawPath(outer, inner, join, stroke, target, pipelineBuilder, color, viewMatrix, true);
    return;
}

