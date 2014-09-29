
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

bool SkRasterizer::rasterizeGPU(const SkDraw &draw, GrContext *context, SkPaint paint, const SkPath& path,
                                SkRect mask, GrAutoScratchTexture* maskTexture, SkBaseDevice *device) const {
    return this->onRasterizeGPU(draw, context, paint, path, mask, maskTexture, device);
}

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


bool SkRasterizer::onRasterizeGPU(const SkDraw& dr, GrContext *context, SkPaint paint, const SkPath& path,
                                  SkRect mask, GrAutoScratchTexture* maskTexture, SkBaseDevice *device) const {
    return false;
}

bool SkRasterizer::canRasterizeGPU(const SkPath& path, const SkMatrix& matrix, const SkIRect* clipBounds,
                                   SkMask* mask, SkMask::CreateMode mode) const {
    return this->canRasterizeGPU(path, matrix, clipBounds, mask, mode);
}
