/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrNonAAStrokeRectBatch.h"

#include "GrBatchTest.h"
#include "GrBatchFlushState.h"
#include "GrColor.h"
#include "GrDefaultGeoProcFactory.h"
#include "GrVertexBatch.h"
#include "SkRandom.h"
#include <GrContext.h>

/*  create a triangle strip that strokes the specified rect. There are 8
    unique vertices, but we repeat the last 2 to close up. Alternatively we
    could use an indices array, and then only send 8 verts, but not sure that
    would be faster.
    */

struct RectVertex
{
    SkPoint pt;
    GrColor color;
};

static inline void fill_indices_1(uint16_t* indices, const int count) {
    for (int i = 0; i < count; i++) {
        indices[i] = i;
    }
}

GrIndexBuffer* indexBuffer_1 = NULL;

static const int MAX_POINTS_1 = 1 << 11;

////////////////////////////////////////////////////////////////////////////////

static GrIndexBuffer* getIndexBuffer_1(GrGpu* gpu) {
    static GrIndexBuffer* fIndexBuffer;
    if (fIndexBuffer == NULL) {
        fIndexBuffer = gpu->createIndexBuffer(MAX_POINTS_1 * sizeof(uint16_t), false);
        if (NULL != fIndexBuffer) {
            // FIXME: Use lock/unlock when port to later version.
            uint16_t* indices = (uint16_t*) fIndexBuffer->map();
            if (NULL != indices) {
                fill_indices_1(indices, MAX_POINTS_1);
                fIndexBuffer->unmap();
            } else {
                indices = (uint16_t*) sk_malloc_throw(sizeof(uint16_t) * MAX_POINTS_1);
                fill_indices_1(indices, MAX_POINTS_1);
                if (!fIndexBuffer->updateData(indices, MAX_POINTS_1 * sizeof(uint16_t))) {
                    fIndexBuffer->unref();
                    fIndexBuffer = NULL;
                }
                sk_free(indices);
            }
        }
    }
    return fIndexBuffer;
}



static void init_stroke_rect_strip(SkPoint verts[10], const SkRect& rect, SkScalar width) {
    const SkScalar rad = SkScalarHalf(width);
    // TODO we should be able to enable this assert, but we'd have to filter these draws
    // this is a bug
    //SkASSERT(rad < rect.width() / 2 && rad < rect.height() / 2);

    verts[0].set(rect.fLeft + rad, rect.fTop + rad);
    verts[1].set(rect.fLeft - rad, rect.fTop - rad);
    verts[2].set(rect.fRight - rad, rect.fTop + rad);
    verts[3].set(rect.fRight + rad, rect.fTop - rad);
    verts[4].set(rect.fRight - rad, rect.fBottom - rad);
    verts[5].set(rect.fRight + rad, rect.fBottom + rad);
    verts[6].set(rect.fLeft + rad, rect.fBottom - rad);
    verts[7].set(rect.fLeft - rad, rect.fBottom + rad);
    verts[8] = verts[0];
    verts[9] = verts[1];
}

class NonAAStrokeRectBatch : public GrVertexBatch {
public:
    DEFINE_BATCH_CLASS_ID

    struct Geometry {
        SkMatrix fViewMatrix;
        SkRect fRect;
        SkScalar fStrokeWidth;
        GrColor fColor;
    };

    static NonAAStrokeRectBatch* Create() {
        return new NonAAStrokeRectBatch;
    }

    const char* name() const override { return "GrStrokeRectBatch"; }

    void getInvariantOutputColor(GrInitInvariantOutput* out) const override {
        // When this is called on a batch, there is only one geometry bundle
        out->setKnownFourComponents(fGeoData[0].fColor);
    }

    void getInvariantOutputCoverage(GrInitInvariantOutput* out) const override {
        out->setKnownSingleComponent(0xff);
    }

    void append(GrColor color, const SkMatrix& viewMatrix, const SkRect& rect,
                SkScalar strokeWidth) {
        Geometry geometry;
        geometry.fViewMatrix = viewMatrix;
        geometry.fRect = rect;
        geometry.fStrokeWidth = strokeWidth;
        geometry.fColor = color;
        fGeoData.push_back(geometry);
    }

    void appendAndUpdateBounds(GrColor color, const SkMatrix& viewMatrix, const SkRect& rect,
                               SkScalar strokeWidth, bool snapToPixelCenters) {
        this->append(color, viewMatrix, rect, strokeWidth);

        SkRect bounds;
        this->setupBounds(&bounds, fGeoData.back(), snapToPixelCenters);
        this->joinBounds(bounds);
    }

    void init(bool snapToPixelCenters) {
        const Geometry& geo = fGeoData[0];
        fBatch.fHairline = geo.fStrokeWidth == 1;

        // setup bounds
        this->setupBounds(&fBounds, geo, snapToPixelCenters);
    }

private:
    void setupBounds(SkRect* bounds, const Geometry& geo, bool snapToPixelCenters) {
        *bounds = geo.fRect;
        SkScalar rad = SkScalarHalf(geo.fStrokeWidth);
        bounds->outset(rad, rad);
        geo.fViewMatrix.mapRect(&fBounds);

        // If our caller snaps to pixel centers then we have to round out the bounds
        if (snapToPixelCenters) {
            bounds->roundOut();
        }
    }

    void onPrepareDraws(Target* target) override {

        Geometry& args = fGeoData[0];
        SkAutoTUnref<const GrGeometryProcessor> gp;
        {
            using namespace GrDefaultGeoProcFactory;
            Color color(this->color());
            if (args.fStrokeWidth > 1 || this->usesLocalCoords())
                color.fType = Color::kUniform_Type;
            else
                color.fType = Color::kAttribute_Type;
            Coverage coverage(this->coverageIgnored() ? Coverage::kSolid_Type :
                                                        Coverage::kNone_Type);
            LocalCoords localCoords(this->usesLocalCoords() ? LocalCoords::kUsePosition_Type :
                                                              LocalCoords::kUnused_Type);
            gp.reset(GrDefaultGeoProcFactory::Create(color, coverage, localCoords,
                                                     this->viewMatrix()));
        }

        target->initDraw(gp, this->pipeline());

        size_t vertexStride = gp->getVertexStride();

        SkASSERT(vertexStride == sizeof(GrDefaultGeoProcFactory::PositionAttr));


        int vertexCount = kVertsPerHairlineRect+3;
        if (args.fStrokeWidth > 1|| this->usesLocalCoords()) {
            vertexCount = kVertsPerStrokeRect;
        }
        const GrVertexBuffer* vertexBuffer;
        int firstVertex;

        int instanceCount = fGeoData.count();
        void* verts = target->makeVertexSpace(vertexStride, instanceCount*vertexCount, &vertexBuffer,
                                              &firstVertex);

        if (!verts || (!indexBuffer_1 && args.fStrokeWidth<2)) {
            SkDebugf("Could not allocate vertices\n");
            return;
        }
        GrPrimitiveType primType = kLines_GrPrimitiveType;
        RectVertex* vertex = reinterpret_cast<RectVertex*>(verts);
        SkPoint* s_vertex = reinterpret_cast<SkPoint*>(verts);
        for (int i = 0; i < instanceCount; i++) {
           if (args.fStrokeWidth > 1 || this->usesLocalCoords()) {
                primType = kTriangleStrip_GrPrimitiveType;
                args.fRect.sort();
                init_stroke_rect_strip(s_vertex, args.fRect, args.fStrokeWidth);
            } else {
                args = fGeoData[i];
                SkRect localrect = args.fRect;
                if(instanceCount > 1)
                    args.fViewMatrix.mapRect(&localrect, args.fRect);
                primType = kLines_GrPrimitiveType;

                (*vertex).pt.set(localrect.fLeft, localrect.fTop);
                (*vertex).color = args.fColor;
                vertex++;
                (*vertex).pt.set(localrect.fRight, localrect.fTop);
                (*vertex).color = args.fColor;
                vertex++;
                (*vertex).pt.set(localrect.fLeft, localrect.fTop);
                (*vertex).color = args.fColor;
                vertex++;
                (*vertex).pt.set(localrect.fLeft, localrect.fBottom);
                (*vertex).color = args.fColor;
                vertex++;
                (*vertex).pt.set(localrect.fLeft, localrect.fBottom);
                (*vertex).color = args.fColor;
                vertex++;
                (*vertex).pt.set(localrect.fRight, localrect.fBottom);
                (*vertex).color = args.fColor;
                vertex++;
                (*vertex).pt.set(localrect.fRight, localrect.fBottom);
                (*vertex).color = args.fColor;
                vertex++;
                (*vertex).pt.set(localrect.fRight, localrect.fTop);
                (*vertex).color = args.fColor;
                vertex++;
            }
        }
        GrVertices vertices;
        if (args.fStrokeWidth > 1 || this->usesLocalCoords())
            vertices.init(primType, vertexBuffer, firstVertex, vertexCount);
        else
            vertices.initInstanced(primType, vertexBuffer, indexBuffer_1,
                                   firstVertex, 8, 8, instanceCount,
                                   MAX_POINTS_1/8);
        target->draw(vertices);
    }

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
    }

    NonAAStrokeRectBatch() : INHERITED(ClassID()) {}

    GrColor color() const { return fBatch.fColor; }
    bool usesLocalCoords() const { return fBatch.fUsesLocalCoords; }
    bool colorIgnored() const { return fBatch.fColorIgnored; }
    const SkMatrix& viewMatrix() const { return fGeoData[0].fViewMatrix; }
    bool hairline() const { return fBatch.fHairline; }
    bool coverageIgnored() const { return fBatch.fCoverageIgnored; }
    SkScalar stroke() const { return fGeoData[0].fStrokeWidth; }

    bool onCombineIfPossible(GrBatch* t, const GrCaps& caps) override {
        // if (!GrPipeline::CanCombine(*this->pipeline(), this->bounds(), *t->pipeline(),
        //     t->bounds(), caps)) {
        //     return false;
        // }
        // GrStrokeRectBatch* that = t->cast<StrokeRectBatch>();

        // NonAA stroke rects other than hairlines cannot batch right now
        // TODO make these batchable

        NonAAStrokeRectBatch* that = t->cast<NonAAStrokeRectBatch>();
        if(this->stroke() != that->stroke())
            return false;

        if (fGeoData[0].fStrokeWidth > 1 || this->usesLocalCoords())
            return false;

        if (usesLocalCoords())
            return false;

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

        SkASSERT(this->usesLocalCoords() == that->usesLocalCoords());
        if (this->usesLocalCoords() && !this->viewMatrix().cheapEqualTo(that->viewMatrix())) {
            return false;
        }

        fGeoData.push_back_n(that->fGeoData.count(), that->fGeoData.begin());
        this->joinBounds(that->bounds());
        return true;
    }

    struct BatchTracker {
        GrColor fColor;
        bool fUsesLocalCoords;
        bool fColorIgnored;
        bool fCoverageIgnored;
        bool fHairline;
    };

    const static int kVertsPerHairlineRect = 5;
    const static int kVertsPerStrokeRect = 10;

    BatchTracker fBatch;
    SkSTArray<1, Geometry, true> fGeoData;

    typedef GrVertexBatch INHERITED;
};

namespace GrNonAAStrokeRectBatch {

GrDrawBatch* Create(GrColor color,
                    const SkMatrix& viewMatrix,
                    const SkRect& rect,
                    SkScalar strokeWidth,
                    bool snapToPixelCenters,
                    GrContext *ctx) {
    NonAAStrokeRectBatch* batch = NonAAStrokeRectBatch::Create();
    if (ctx)
        indexBuffer_1 = getIndexBuffer_1(ctx->getGpu());
    batch->append(color, viewMatrix, rect, strokeWidth);
    batch->init(snapToPixelCenters);
    return batch;
}

void Append(GrBatch* origBatch,
            GrColor color,
            const SkMatrix& viewMatrix,
            const SkRect& rect,
            SkScalar strokeWidth,
            bool snapToPixelCenters) {
    NonAAStrokeRectBatch* batch = origBatch->cast<NonAAStrokeRectBatch>();
    batch->appendAndUpdateBounds(color, viewMatrix, rect, strokeWidth, snapToPixelCenters);
}

};

#ifdef GR_TEST_UTILS

DRAW_BATCH_TEST_DEFINE(NonAAStrokeRectBatch) {
    SkMatrix viewMatrix = GrTest::TestMatrix(random);
    GrColor color = GrRandomColor(random);
    SkRect rect = GrTest::TestRect(random);
    SkScalar strokeWidth = random->nextBool() ? 0.0f : 1.0f;

    return GrNonAAStrokeRectBatch::Create(color, viewMatrix, rect, strokeWidth, random->nextBool(), NULL);
}

#endif
