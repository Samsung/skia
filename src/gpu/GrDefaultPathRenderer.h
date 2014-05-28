/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrDefaultPathRenderer_DEFINED
#define GrDefaultPathRenderer_DEFINED

#include "GrPathRenderer.h"
#include "SkTemplates.h"

/**
 *  Subclass that renders the path using the stencil buffer to resolve fill rules
 * (e.g. winding, even-odd)
 */
class SK_API GrDefaultPathRenderer : public GrPathRenderer {
public:
    GrDefaultPathRenderer(bool separateStencilSupport, bool stencilWrapOpsSupport);

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

    bool internalDrawPath(GrDrawTarget*,
                          GrPipelineBuilder*,
                          GrColor,
                          const SkMatrix& viewMatrix,
                          const SkPath&,
                          const SkStrokeRec&,
                          bool stencilOnly);

    bool createGeom(GrDrawTarget*,
                    GrPipelineBuilder*,
                    GrPrimitiveType*,
                    int* vertexCnt,
                    int* indexCnt,
                    GrDrawTarget::AutoReleaseGeometry*,
                    const SkPath&,
                    const SkStrokeRec&,
                    SkScalar srcSpaceTol);

    bool    fSeparateStencil;
    bool    fStencilWrapOps;

    typedef GrPathRenderer INHERITED;
};

#endif
