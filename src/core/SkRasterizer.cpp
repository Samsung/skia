
/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#include "SkRasterizer.h"
#include "SkDraw.h"
#include "SkMaskFilter.h"
#include "SkPath.h"

#if SK_SUPPORT_GPU
#include "GrContext.h"
#include "GrTexture.h"
#endif

bool SkRasterizer::rasterize(const SkPath& fillPath, const SkMatrix& matrix,
                             const SkIRect* clipBounds, SkMaskFilter* filter,
                             SkMask* mask, SkMask::CreateMode mode) const {
    SkIRect storage;

    if (clipBounds && filter && SkMask::kJustRenderImage_CreateMode != mode) {
        SkIPoint    margin;
        SkMask      srcM, dstM;

        srcM.fFormat = SkMask::kA8_Format;
        srcM.fBounds.set(0, 0, 1, 1);
        srcM.fImage = NULL;
        if (!filter->filterMask(&dstM, srcM, matrix, &margin)) {
            return false;
        }
        storage = *clipBounds;
        storage.inset(-margin.fX, -margin.fY);
        clipBounds = &storage;
    }

    return this->onRasterize(fillPath, matrix, clipBounds, mask, mode);
}

/*  Our default implementation of the virtual method just scan converts
*/
bool SkRasterizer::onRasterize(const SkPath& fillPath, const SkMatrix& matrix,
                             const SkIRect* clipBounds,
                             SkMask* mask, SkMask::CreateMode mode) const {
    SkPath  devPath;

    fillPath.transform(matrix, &devPath);
    return SkDraw::DrawToMask(devPath, clipBounds, NULL, NULL, mask, mode,
                              SkPaint::kFill_Style);
}

#if SK_SUPPORT_GPU
bool SkRasterizer::canRasterizeGPU(const SkPath& path,
                                   const SkIRect& clipBounds,
                                   const SkMatrix& matrix,
                                   SkMaskFilter* filter,
                                   SkIRect* rasterRect) const {
    return false;
}

bool SkRasterizer::rasterizeGPU(GrContext* context,
                                const SkPath& path, const SkMatrix& matrix,
                                const SkIRect* clipBounds, bool doAA,
                                SkStrokeRec* stroke, GrTexture** result,
                                SkMask::CreateMode mode) const {
    return this->onRasterizeGPU(context, path, matrix, clipBounds, doAA,
                                stroke, result, mode);
}

bool SkRasterizer::onRasterizeGPU(GrContext* context,
                                  const SkPath& path,
                                  const SkMatrix& matrix,
                                  const SkIRect* clipBounds,
                                  bool doAA,
                                  SkStrokeRec* stroke,
                                  GrTexture** result,
                                  SkMask::CreateMode mode) const {
    return false;
}
#endif
