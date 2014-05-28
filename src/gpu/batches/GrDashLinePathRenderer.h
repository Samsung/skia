
/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrDashLinePathRenderer_DEFINED
#define GrDashLinePathRenderer_DEFINED

#include "GrPathRenderer.h"

class GrDashLinePathRenderer : public GrPathRenderer {
private:
    bool onCanDrawPath(const CanDrawPathArgs&) const override;

    StencilSupport onGetStencilSupport(const SkPath&, const GrStrokeInfo&) const override {
        return kNoSupport_StencilSupport;
    }

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

    SkAutoTUnref<GrGpu> fGpu;
    typedef GrPathRenderer INHERITED;
};


#endif
