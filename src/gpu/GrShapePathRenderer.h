/*
 * Copyright 2014 Samsung Research America, Inc. - SiliconValley
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrShapePathRenderer_DEFINED
#define GrShapePathRenderer_DEFINED

#include "GrPathRenderer.h"
#include "SkTemplates.h"

/**
 *  Subclass that renders the path using the stencil buffer to resolve fill rules
 * (e.g. winding, even-odd)
 */
class SK_API GrShapePathRenderer : public GrPathRenderer {
public:
    GrShapePathRenderer();
    bool onCanDrawPath(const CanDrawPathArgs& args) const override {
		return false;
    }

    bool onDrawPath(const DrawPathArgs& args) override {
		return false;
    }

    bool canDrawPath(const SkPath&,
                     const SkPath&,
                     const SkPath&,
                     const GrStrokeInfo& stroke,
                     const GrDrawTarget*,
                     GrPipelineBuilder* pipelineBuilder,
                     GrColor color,
                     const SkMatrix& viewMatrix,
                     bool antiAlias) const override;

    GrPathRenderer::StencilSupport onGetStencilSupport(const SkPath&,
                                                       const GrStrokeInfo&) const override;

private:
    virtual bool onDrawPath(const SkPath&,
                            const SkPath&,
                            const SkPath&,
                            const GrStrokeInfo&,
                            GrDrawTarget*,
                            GrPipelineBuilder* pipelineBuilder,
                            GrColor color,
                            const SkMatrix& viewMatrix,
                            bool isOpaque) override;

    void onStencilPath(const SkPath&,
                       const SkPath&,
                       const SkPath&,
                       const GrStrokeInfo&,
                       GrDrawTarget*,
                       GrPipelineBuilder* pipelineBuilder,
                       GrColor color,
                       const SkMatrix& viewMatrix) override;

    bool internalDrawPath(const SkPath&,
                          const SkPath&,
                          const SkPath&,
                          const GrStrokeInfo&,
                          GrDrawTarget*,
                          GrPipelineBuilder* pipelineBuilder,
                          GrColor color,
                          const SkMatrix& viewMatrix,
                          bool isOpaque);

    typedef GrPathRenderer INHERITED;
};

#endif
