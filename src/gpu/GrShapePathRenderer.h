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

    virtual bool canDrawPath(const SkPath&,
                             const SkStrokeRec&,
                             const GrDrawTarget*,
                             bool antiAlias) const SK_OVERRIDE {
        return false;
    }

    virtual bool canDrawPath(const SkPath&,
                             const SkPath&,
                             const SkPath&,
                             const SkStrokeRec&,
                             const GrDrawTarget*,
                             bool antiAlias) const SK_OVERRIDE;

private:

    virtual StencilSupport onGetStencilSupport(const SkPath&,
                                               const SkStrokeRec&,
                                               const GrDrawTarget*) const SK_OVERRIDE;

    virtual bool onDrawPath(const SkPath&,
                            const SkPath&,
                            const SkPath&,
                            const SkStrokeRec&,
                            GrDrawTarget*,
                            bool isOpaque) SK_OVERRIDE;

    virtual bool onDrawPath(const SkPath&,
                            const SkStrokeRec&,
                            GrDrawTarget*,
                            bool antiAlias) SK_OVERRIDE;

    virtual void onStencilPath(const SkPath&,
                               const SkPath&,
                               const SkPath&,
                               const SkStrokeRec&,
                               GrDrawTarget*) SK_OVERRIDE;

    virtual void onStencilPath(const SkPath&,
                               const SkStrokeRec&,
                               GrDrawTarget*) SK_OVERRIDE;

    bool internalDrawPath(const SkPath&,
                          const SkPath&,
                          const SkPath&,
                          const SkStrokeRec&,
                          GrDrawTarget*,
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
