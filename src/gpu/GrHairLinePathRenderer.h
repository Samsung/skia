/*
 * Copyright 2014 Samsung Electronics. All rights reserved.
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
    GrHairLinePathRenderer() : fNumPts(0) {}
    ~GrHairLinePathRenderer();

    bool onCanDrawPath(const CanDrawPathArgs& args) const override {
        return canDrawPath(NULL, args.fPipelineBuilder, *args.fViewMatrix,
                           *args.fPath, *args.fStroke, args.fAntiAlias);
    }

    bool onDrawPath(const DrawPathArgs& args) override {
        return onDrawPath(args.fTarget, args.fPipelineBuilder, args.fColor,
                          *args.fViewMatrix, *args.fPath, *args.fStroke, args.fAntiAlias);
    }

    bool canDrawPath(const GrDrawTarget* target,
                             const GrPipelineBuilder* pipelineBuilder,
                             const SkMatrix& viewMatrix,
                             const SkPath& path,
                             const GrStrokeInfo& rec,
                             bool antiAlias) const;

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

private:
    bool onDrawPath(GrDrawTarget*,
                    GrPipelineBuilder*,
                    GrColor,
                    const SkMatrix& viewMatrix,
                    const SkPath&,
                    const GrStrokeInfo&,
                    bool antiAlias);

    bool internalDrawPath(GrDrawTarget*,
                          GrPipelineBuilder*,
                          GrColor,
                          const SkMatrix& viewMatrix,
                          const SkPath&,
                          const GrStrokeInfo&,
                          bool stencilOnly);

    void setNumberOfPts(int numPts) { fNumPts = numPts; }

    typedef GrPathRenderer INHERITED;

    int fNumPts;
};

#endif
