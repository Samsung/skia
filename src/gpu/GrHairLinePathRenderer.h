/*
 * Copyright 2014 Samsung Research America, Inc - Silicon Valley
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
    GrHairLinePathRenderer() : fIndexBuffer(NULL), fNumPts(0) {}
    ~GrHairLinePathRenderer();

    virtual bool canDrawPath(const SkPath&,
                             const SkStrokeRec&,
                             const GrDrawTarget*,
                             bool antiAlias) const SK_OVERRIDE;

    virtual bool canDrawPath(const SkPath&,
                             const SkPath&,
                             const SkPath&,
                             const SkStrokeRec&,
                             const GrDrawTarget*,
                             bool antiAlias) const SK_OVERRIDE {
        return false;
    }

private:

    virtual StencilSupport onGetStencilSupport(const SkPath&,
                                               const SkStrokeRec&,
                                               const GrDrawTarget*) const SK_OVERRIDE;

    virtual bool onDrawPath(const SkPath&,
                            const SkStrokeRec&,
                            GrDrawTarget*,
                            bool antiAlias) SK_OVERRIDE;

    virtual bool onDrawPath(const SkPath&,
                            const SkPath&,
                            const SkPath&, 
                            const SkStrokeRec&,
                            GrDrawTarget*,
                            bool antiAlias) SK_OVERRIDE {
        return false;
    }

    virtual void onStencilPath(const SkPath&,
                               const SkStrokeRec&,
                               GrDrawTarget*) SK_OVERRIDE {}

    bool internalDrawPath(const SkPath&,
                          const SkStrokeRec&,
                          GrDrawTarget*,
                          bool stencilOnly);

    GrIndexBuffer* indexBuffer(GrGpu* gpu);

    void setNumberOfPts(int numPts) { fNumPts = numPts; }

    typedef GrPathRenderer INHERITED;

    GrIndexBuffer* fIndexBuffer;
    int fNumPts;
};

#endif
