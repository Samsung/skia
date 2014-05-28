/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrTessellatingPathRenderer_DEFINED
#define GrTessellatingPathRenderer_DEFINED

#include "GrPathRenderer.h"

/**
 *  Subclass that renders the path by converting to screen-space trapezoids plus
 *   extra 1-pixel geometry for AA.
 */
class SK_API GrTessellatingPathRenderer : public GrPathRenderer {
public:
    GrTessellatingPathRenderer();

private:
    bool onCanDrawPath(const CanDrawPathArgs& ) const override;

    bool canDrawPath(const SkPath&,
                     const SkPath&,
                     const SkPath&,
                     const GrStrokeInfo&,
                     const GrDrawTarget*,
                     GrPipelineBuilder* pipelineBuilder,
                     GrColor color,
                     const SkMatrix& viewMatrix,
                     bool antiAlias) const override {
        return false;
    }

    bool onDrawPath(const SkPath&,
                    const SkPath&,
                    const SkPath&,
                    const GrStrokeInfo&,
                    GrDrawTarget*,
                    GrPipelineBuilder*,
                    GrColor color,
                    const SkMatrix& viewMatrix,
                    bool antiAlias) override {
        return false;
    }

    void onStencilPath(const SkPath&,
                       const SkPath&,
                       const SkPath&,
                       const GrStrokeInfo&,
                       GrDrawTarget*,
                       GrPipelineBuilder* pipelineBuilder,
                       GrColor color,
                       const SkMatrix& viewMatrix) {
        return;
    }

    StencilSupport onGetStencilSupport(const SkPath&, const GrStrokeInfo&) const override {
        return GrPathRenderer::kNoSupport_StencilSupport;
    }

    bool onDrawPath(const DrawPathArgs&) override;

    typedef GrPathRenderer INHERITED;
};

#endif
