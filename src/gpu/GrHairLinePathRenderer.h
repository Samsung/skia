/*
 * Copyright 2014 Samsung Research America, Inc - Silicon Valley
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrHairLinePathRenderer_DEFINED
#define GrHairLinePathRenderer_DEFINED

#include "GrPathRenderer.h"
#include "SkTemplates.h"
#include "GrGpu.h"

/**
 *  Subclass that renders the path using GL_LINES
 */
class SK_API GrHairLinePathRenderer : public GrPathRenderer {
public:
    GrHairLinePathRenderer() : fIndexBuffer(NULL), fNumPts(0) {}
    ~GrHairLinePathRenderer();

    virtual bool canDrawPath(const GrDrawTarget* target,
                             const GrPipelineBuilder* pipelineBuilder,
                             const SkMatrix& viewMatrix,
                             const SkPath& path,
                             const SkStrokeRec& rec,
                             bool antiAlias) const SK_OVERRIDE;

    virtual bool canDrawPath(const SkPath& pathA,
                             const SkPath& pathB,
                             const SkPath& pathC,
                             const SkStrokeRec& rec,
                             const GrDrawTarget* target,
                             GrPipelineBuilder* pipelineBuilder,
                             GrColor color,
                             const SkMatrix& viewMatrix,
                             bool antiAlias)  const SK_OVERRIDE {
    return false;
    }

private:
    virtual StencilSupport onGetStencilSupport(const GrDrawTarget*,
                                               const GrPipelineBuilder*,
                                               const SkPath&,
                                               const SkStrokeRec&) const SK_OVERRIDE;

    virtual bool onDrawPath(GrDrawTarget*,
                            GrPipelineBuilder*,
                            GrColor,
                            const SkMatrix& viewMatrix,
                            const SkPath&,
                            const SkStrokeRec&,
                            bool antiAlias) SK_OVERRIDE;

    virtual bool onDrawPath(const SkPath& pathA,
                            const SkPath& pathB,
                            const SkPath& pathC,
                            const SkStrokeRec& stroke,
                            GrDrawTarget* target,
                            GrPipelineBuilder*,
                            GrColor color,
                            const SkMatrix& viewMatrix,
                            bool antiAlias) SK_OVERRIDE {
    return false;
    }

    bool internalDrawPath(GrDrawTarget*,
                          GrPipelineBuilder*,
                          GrColor,
                          const SkMatrix& viewMatrix,
                          const SkPath&,
                          const SkStrokeRec&,
                          bool stencilOnly);

    GrIndexBuffer* indexBuffer(GrGpu* gpu);

    void setNumberOfPts(int numPts) { fNumPts = numPts; }

    typedef GrPathRenderer INHERITED;

    GrIndexBuffer* fIndexBuffer;
    int fNumPts;
};

#endif
