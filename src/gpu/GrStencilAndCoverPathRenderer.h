
/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrBuiltInPathRenderer_DEFINED
#define GrBuiltInPathRenderer_DEFINED

#include "GrPathRenderer.h"

class GrContext;
class GrGpu;

/**
 * Uses GrGpu::stencilPath followed by a cover rectangle. This subclass doesn't apply AA; it relies
 * on the target having MSAA if AA is desired.
 */
class GrStencilAndCoverPathRenderer : public GrPathRenderer {
public:

    static GrPathRenderer* Create(GrContext*);

    virtual ~GrStencilAndCoverPathRenderer();

    virtual bool canDrawPath(const GrDrawTarget*,
                             const GrPipelineBuilder*,
                             const SkMatrix& viewMatrix,
                             const SkPath&,
                             const SkStrokeRec&,
                             bool antiAlias) const SK_OVERRIDE;

    virtual bool canDrawPath(const SkPath&,
                             const SkPath&,
                             const SkPath&,
                             const SkStrokeRec&,
                             const GrDrawTarget*,
                             GrPipelineBuilder* pipelineBuilder,
                             GrColor color,
                             const SkMatrix& viewMatrix,
                             bool antiAlias) const SK_OVERRIDE {
        return false;
    }

protected:
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

    virtual void onStencilPath(GrDrawTarget*,
                               GrPipelineBuilder*,
                               const SkMatrix& viewMatrix,
                               const SkPath&,
                               const SkStrokeRec&) SK_OVERRIDE;

    virtual bool onDrawPath(const SkPath&,
                            const SkPath&,
                            const SkPath&,
                            const SkStrokeRec&,
                            GrDrawTarget*,
                            GrPipelineBuilder*,
                            GrColor color,
                            const SkMatrix& viewMatrix,
                            bool antiAlias) SK_OVERRIDE {
        return false;
    }

    virtual void onStencilPath(const SkPath&,
                               const SkPath&,
                               const SkPath&,
                               const SkStrokeRec&,
                               GrDrawTarget*,
                               GrPipelineBuilder* pipelineBuilder,
                               GrColor color,
                               const SkMatrix& viewMatrix) {}

private:
    GrStencilAndCoverPathRenderer(GrGpu*);

    GrGpu* fGpu;

    typedef GrPathRenderer INHERITED;
};

#endif
