
/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrPathRenderer.h"


class GrAAConvexPathRenderer : public GrPathRenderer {
public:
    GrAAConvexPathRenderer();

    virtual bool canDrawPath(const SkPath& path,
                             const SkStrokeRec& stroke,
                             const GrDrawTarget* target,
                             bool antiAlias) const SK_OVERRIDE;

    virtual bool canDrawPath(const SkPath& pathA,
                             const SkPath& pathB,
                             const SkPath& pathC,
                             const SkStrokeRec& stroke,
                             const GrDrawTarget* target,
                             bool antiAlias) const SK_OVERRIDE {
        return false;
    }

protected:
    virtual bool onDrawPath(const SkPath& path,
                            const SkStrokeRec& stroke,
                            GrDrawTarget* target,
                            bool antiAlias) SK_OVERRIDE;

    virtual bool onDrawPath(const SkPath& pathA,
                            const SkPath& pathB,
                            const SkPath& pathC,
                            const SkStrokeRec& stroke,
                            GrDrawTarget* target,
                            bool antiAlias) SK_OVERRIDE {
        return false;
    }
};
