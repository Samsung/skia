
/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrAAHairLinePathRenderer_DEFINED
#define GrAAHairLinePathRenderer_DEFINED

#include "GrPathRenderer.h"

class GrAAHairLinePathRenderer : public GrPathRenderer {
public:
    virtual ~GrAAHairLinePathRenderer();

    static GrPathRenderer* Create(GrContext* context);

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

    typedef SkTArray<SkPoint, true> PtArray;
    typedef SkTArray<int, true> IntArray;
    typedef SkTArray<float, true> FloatArray;

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

    virtual void onStencilPath(const SkPath&,
                               const SkPath&,
                               const SkPath&,
                               const SkStrokeRec&,
                               GrDrawTarget*,
                               GrPipelineBuilder* pipelineBuilder,
                               GrColor color,
                               const SkMatrix& viewMatrix) {}

private:
    GrAAHairLinePathRenderer(const GrContext* context,
                             const GrIndexBuffer* fLinesIndexBuffer,
                             const GrIndexBuffer* fQuadsIndexBuffer);

    const GrIndexBuffer*        fLinesIndexBuffer;
    const GrIndexBuffer*        fQuadsIndexBuffer;

    typedef GrPathRenderer INHERITED;
};


#endif
