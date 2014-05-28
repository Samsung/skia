
/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrAAConvexPathRenderer_DEFINED
#define GrAAConvexPathRenderer_DEFINED

#include "GrPathRenderer.h"

class GrAAConvexPathRenderer : public GrPathRenderer {
public:
    GrAAConvexPathRenderer();

    virtual bool canDrawPath(const GrDrawTarget*,
                             const GrPipelineBuilder*,
                             const SkMatrix& viewMatrix,
                             const SkPath&,
                             const SkStrokeRec&,
                             bool antiAlias) const SK_OVERRIDE;

    virtual bool canDrawPath(const SkPath& pathA,
                             const SkPath& pathB,
                             const SkPath& pathC,
                             const SkStrokeRec& stroke,
                             const GrDrawTarget* target,
                             GrPipelineBuilder* pipelineBuilder,
                             GrColor color,
                             const SkMatrix& viewMatrix,
                             bool antiAlias) const SK_OVERRIDE {
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

protected:
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
};

#endif
