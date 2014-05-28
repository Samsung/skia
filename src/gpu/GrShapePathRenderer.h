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

    virtual bool canDrawPath(const GrDrawTarget*,
                             const GrPipelineBuilder*,
                             const SkMatrix& viewMatrix,
                             const SkPath&,
                             const SkStrokeRec&,
                             bool antiAlias) const SK_OVERRIDE { return false; }

    virtual bool canDrawPath(const SkPath&,
                             const SkPath&,
                             const SkPath&,
                             const SkStrokeRec&,
                             const GrDrawTarget*,
                             GrPipelineBuilder* pipelineBuilder,
                             GrColor color,
                             const SkMatrix& viewMatrix,
                             bool antiAlias) const SK_OVERRIDE;

private:

    GrPathRenderer::StencilSupport onGetStencilSupport(const GrDrawTarget*,
                                                       const GrPipelineBuilder*,
                                                       const SkPath&,
                                                       const SkStrokeRec&) const SK_OVERRIDE;

    virtual bool onDrawPath(const SkPath&,
                            const SkPath&,
                            const SkPath&,
                            const SkStrokeRec&,
                            GrDrawTarget*,
                            GrPipelineBuilder* pipelineBuilder,
                            GrColor color,
                            const SkMatrix& viewMatrix,
                            bool isOpaque) SK_OVERRIDE;

    virtual bool onDrawPath(GrDrawTarget*,
                            GrPipelineBuilder*,
                            GrColor,
                            const SkMatrix& viewMatrix,
                            const SkPath&,
                            const SkStrokeRec&,
                            bool antiAlias) SK_OVERRIDE { return false; }

    virtual void onStencilPath(const SkPath&,
                               const SkPath&,
                               const SkPath&,
                               const SkStrokeRec&,
                               GrDrawTarget*,
                               GrPipelineBuilder* pipelineBuilder,
                               GrColor color,
                               const SkMatrix& viewMatrix);

    bool internalDrawPath(const SkPath&,
                          const SkPath&,
                          const SkPath&,
                          const SkStrokeRec&,
                          GrDrawTarget*,
                          GrPipelineBuilder* pipelineBuilder,
                          GrColor color,
                          const SkMatrix& viewMatrix,
                          bool isOpaque);

    bool createGeom(const SkPath&, const SkPath&, const SkPath&,
                    SkScalar srcSpaceTol,
                    GrDrawTarget*,
                    GrPrimitiveType*,
                    int* vertexCnt,
                    GrDrawTarget::AutoReleaseGeometry*);

    typedef GrPathRenderer INHERITED;
};

#endif
