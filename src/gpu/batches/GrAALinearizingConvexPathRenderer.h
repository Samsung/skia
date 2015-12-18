
/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrAALinearizingConvexPathRenderer_DEFINED
#define GrAALinearizingConvexPathRenderer_DEFINED

#include "GrPathRenderer.h"

class GrAALinearizingConvexPathRenderer : public GrPathRenderer {
public:
    GrAALinearizingConvexPathRenderer();

private:
    bool onCanDrawPath(const CanDrawPathArgs&) const override;

    bool onDrawPath(const DrawPathArgs&) override;

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
};

#endif
