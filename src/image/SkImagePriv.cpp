#include "SkImagePriv.h"

SkBitmap::Config SkImageInfoToBitmapConfig(const SkImage::Info& info,
                                           bool* isOpaque) {
    switch (info.fColorType) {
        case SkImage::kAlpha_8_ColorType:
            switch (info.fAlphaType) {
                case SkImage::kIgnore_AlphaType:
                    // makes no sense
                    return SkBitmap::kNo_Config;

                case SkImage::kOpaque_AlphaType:
                    *isOpaque = true;
                    return SkBitmap::kA8_Config;

                case SkImage::kPremul_AlphaType:
                case SkImage::kUnpremul_AlphaType:
                    *isOpaque = false;
                    return SkBitmap::kA8_Config;
            }
            break;

        case SkImage::kRGB_565_ColorType:
            // we ignore fAlpahType, though some would not make sense
            *isOpaque = true;
            return SkBitmap::kRGB_565_Config;

        case SkImage::kRGBA_8888_ColorType:
        case SkImage::kBGRA_8888_ColorType:
            // not supported yet
            return SkBitmap::kNo_Config;

        case SkImage::kPMColor_ColorType:
            switch (info.fAlphaType) {
                case SkImage::kIgnore_AlphaType:
                case SkImage::kUnpremul_AlphaType:
                    // not supported yet
                    return SkBitmap::kNo_Config;
                case SkImage::kOpaque_AlphaType:
                    *isOpaque = true;
                    return SkBitmap::kARGB_8888_Config;
                case SkImage::kPremul_AlphaType:
                    *isOpaque = false;
                    return SkBitmap::kARGB_8888_Config;
            }
            break;
    }
    SkASSERT(!"how did we get here");
    return SkBitmap::kNo_Config;
}

int SkImageBytesPerPixel(SkImage::ColorType ct) {
    static const uint8_t gColorTypeBytesPerPixel[] = {
        1,  // kAlpha_8_ColorType
        2,  // kRGB_565_ColorType
        4,  // kRGBA_8888_ColorType
        4,  // kBGRA_8888_ColorType
        4,  // kPMColor_ColorType
    };

    SkASSERT((size_t)ct < SK_ARRAY_COUNT(gColorTypeBytesPerPixel));
    return gColorTypeBytesPerPixel[ct];
}

bool SkBitmapToImageInfo(const SkBitmap& bm, SkImage::Info* info) {
    switch (bm.config()) {
        case SkBitmap::kA8_Config:
            info->fColorType = SkImage::kAlpha_8_ColorType;
            break;

        case SkBitmap::kRGB_565_Config:
            info->fColorType = SkImage::kRGB_565_ColorType;
            break;
            
        case SkBitmap::kARGB_8888_Config:
            info->fColorType = SkImage::kPMColor_ColorType;
            break;
        
        default:
            return false;
    }
    
    info->fWidth = bm.width();
    info->fHeight = bm.height();
    info->fAlphaType = bm.isOpaque() ? SkImage::kOpaque_AlphaType :
                                       SkImage::kPremul_AlphaType;
    return true;
}

SkImage* SkNewImageFromBitmap(const SkBitmap& bm) {
    SkImage::Info info;
    if (!SkBitmapToImageInfo(bm, &info)) {
        return NULL;
    }

    SkImage* image = NULL;
    if (bm.isImmutable()) {
        image = SkNewImageFromPixelRef(info, bm.pixelRef(), bm.rowBytes());
    } else {
        bm.lockPixels();
        if (NULL == bm.getPixels()) {
            image = SkImage::NewRasterCopy(info, NULL, bm.getPixels(),
                                           bm.rowBytes());
        }
        bm.unlockPixels();
    }
    return image;
}

