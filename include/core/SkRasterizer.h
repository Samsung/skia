
/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#ifndef SkRasterizer_DEFINED
#define SkRasterizer_DEFINED

#include "SkFlattenable.h"
#include "SkMask.h"

class SkMaskFilter;
class SkMatrix;
class SkPath;
class GrContext;
class GrTexture;
struct SkIRect;
class SkStrokeRec;

class SK_API SkRasterizer : public SkFlattenable {
public:
    SK_DECLARE_INST_COUNT(SkRasterizer)

    /** Turn the path into a mask, respecting the specified local->device matrix.
    */
    bool rasterize(const SkPath& path, const SkMatrix& matrix,
                   const SkIRect* clipBounds, SkMaskFilter* filter,
                   SkMask* mask, SkMask::CreateMode mode) const;

#if SK_SUPPORT_GPU
    virtual bool canRasterizeGPU(const SkPath& path,
                                 const SkIRect& clipBounds,
                                 const SkMatrix& matrix,
                                 SkMaskFilter* filter,
                                 SkIRect* rasterRect) const;

    virtual bool rasterizeGPU(GrContext* context,
                              const SkPath& path, const SkMatrix& matrix,
                              const SkIRect* clipBounds,
                              bool doAA,
                              SkStrokeRec* stroke,
                              GrTexture** result,
                              SkMask::CreateMode mode) const;
#endif

    SK_DEFINE_FLATTENABLE_TYPE(SkRasterizer)

protected:
    SkRasterizer() {}
    SkRasterizer(SkReadBuffer& buffer) : INHERITED(buffer) {}

    virtual bool onRasterize(const SkPath& path, const SkMatrix& matrix,
                             const SkIRect* clipBounds,
                             SkMask* mask, SkMask::CreateMode mode) const;

    virtual bool onRasterizeGPU(GrContext* context,
                                const SkPath& path, const SkMatrix& matrix,
                                const SkIRect* clipBounds, bool doAA,
                                SkStrokeRec* stroke,
                                GrTexture** result, SkMask::CreateMode mode) const;
private:
    typedef SkFlattenable INHERITED;
};

#endif
