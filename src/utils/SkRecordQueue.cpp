/*
 * Copyright 2014 Samsung Research America, Inc - Silicon Valley
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkRecordQueue.h"
#include "SkDrawFilter.h"
#include "SkPaint.h"
#include "SkDeque.h"
#include "SkData.h"
#include "SkDrawFilter.h"
#include "SkRect.h"
#include "SkRRect.h"
#include "SkRegion.h"
#include "SkBitmap.h"
#include "SkCanvas.h"
#include "SkPath.h"
#include "SkMatrix.h"
#include "SkPoint.h"

typedef void (*PlaybackProc)(SkCanvas *, SkRecordQueue::SkCanvasRecordInfo *, bool);

static void clipPath_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    SkPath* path = command->fPath;
    SkRegion::Op op = (SkRegion::Op) (command->fFlags >> SkRecordQueue::kCanvas_RegionOpFlag);
    bool doAntialias = command->fBool;
    if (!skip)
        canvas->clipPath(*path, op, doAntialias);
    delete path;
}

static void clipRegion_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    SkRegion& region = command->fRegion;
    SkRegion::Op op = (SkRegion::Op) (command->fFlags >> SkRecordQueue::kCanvas_RegionOpFlag);
    if (!skip)
        canvas->clipRegion(region, op);
    region.setEmpty();
}

static void clipRect_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    if (!skip) {
        SkRect& rect = command->fRect1;
        SkRegion::Op op = (SkRegion::Op)(command->fFlags >> SkRecordQueue::kCanvas_RegionOpFlag);
        canvas->clipRect(rect, op, command->fBool);
    }
}

static void clipRRect_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    if (!skip) {
        SkRRect& rrect = command->fRRect1;
        SkRegion::Op op = (SkRegion::Op) (command->fFlags >> SkRecordQueue::kCanvas_RegionOpFlag);
        bool doAntialias = command->fBool;
        canvas->clipRRect(rrect, op, command->fBool);
    }
}

static void setMatrix_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    if (!skip)
        canvas->setMatrix(command->fMatrix);
}

static void concat_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    if (!skip)
        canvas->concat(command->fMatrix);
}

static void scale_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    if (!skip)
        canvas->scale(command->fX, command->fY);
}

static void skew_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    if (!skip)
        canvas->skew(command->fX, command->fY);
}

static void rotate_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    if (!skip)
        canvas->rotate(command->fX);
}

static void translate_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    if (!skip)
        canvas->translate(command->fX, command->fY);
}

static void save_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    canvas->save((SkCanvas::SaveFlags) (command->fFlags >> SkRecordQueue::kCanvas_SaveFlag));
}

static void saveLayer_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    SkRect& bounds = command->fRect1;
    SkPaint& paint = command->fPaint;

    bool validBounds = command->fPtrFlags & SkRecordQueue::kFirst_PointerFlag;
    bool validPaint = command->fPtrFlags & SkRecordQueue::kSecond_PointerFlag;
    SkCanvas::SaveFlags flags = (SkCanvas::SaveFlags)(command->fFlags >> SkRecordQueue::kCanvas_SaveFlag);

    if (!validBounds  && !validPaint) {
        if (flags)
            canvas->saveLayer(NULL, NULL, flags);
        else
            canvas->saveLayer(NULL, NULL);
    }
    else if (!validBounds) {
        if (flags)
            canvas->saveLayer(NULL, &paint, flags);
        else
            canvas->saveLayer(NULL, &paint);
        paint.reset();
    }
    else if (!validPaint) {
        if (flags)
            canvas->saveLayer(&bounds, NULL, flags);
        else
            canvas->saveLayer(&bounds, NULL);
    }
    else {
        if (flags)
            canvas->saveLayer(&bounds, &paint, flags);
        else
            canvas->saveLayer(&bounds, &paint);
        paint.reset();
    }
}

static void restore_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    canvas->restore();
}

static void clear_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    if (!skip)
        canvas->clear(command->fColor);
}

static void drawPaint_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    if (!skip)
        canvas->drawPaint(command->fPaint);
    command->fPaint.reset();
}

static void drawPoints_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    SkPaint& paint = command->fPaint;
    SkPoint* pts = command->fPoints;
    if (!skip) {
        SkCanvas::PointMode pointMode = (SkCanvas::PointMode)(command->fFlags >> SkRecordQueue::kCanvas_PointModeFlag);
        canvas->drawPoints(pointMode, command->fSize, pts, paint);
    }
    sk_free(pts);
    paint.reset();
}

static void drawOval_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    SkPaint& paint = command->fPaint;
    if (!skip) {
        SkRect& rect = command->fRect1;
        canvas->drawOval(rect, paint);
    }
    paint.reset();
}

static void drawRect_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    SkPaint& paint = command->fPaint;
    if (!skip) {
        SkRect& rect = command->fRect1;
        canvas->drawRect(rect, paint);
    }
    paint.reset();
}

static void drawRRect_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    SkPaint& paint = command->fPaint;
    if (!skip) {
        SkRRect& rrect = command->fRRect1;
        canvas->drawRRect(rrect, paint);
    }
    paint.reset();
}

static void drawDRRect_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    SkPaint& paint = command->fPaint;
    if (!skip) {
        SkRRect& outer = command->fRRect1;
        SkRRect& inner = command->fRRect2;
        canvas->drawDRRect(outer, inner, paint);
    }
    paint.reset();
}

static void drawPath_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    SkPath* path = command->fPath;
    SkPaint& paint = command->fPaint;
    if (!skip)
        canvas->drawPath(*path, paint);

    path->reset();
    paint.reset();
    delete path;
}

static void drawVertices_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo *command, bool skip)
{
    SkPaint& paint = command->fPaint;
    SkPoint* vertices = command->fPoints;
    SkColor* colors = command->fColors;
    SkXfermode* xfermode = command->fXfermode;
    SkPoint* texs = command->fTexs;
    uint16_t* indices = command->fIndices;

    if (!skip) {
        SkCanvas::VertexMode vmode = (SkCanvas::VertexMode) (command->fFlags >> SkRecordQueue::kCanvas_VertexModeFlag);
        int vertexCount =  command->fI;
        int indexCount = command->fJ;


        canvas->drawVertices(vmode, vertexCount, vertices, texs, colors,
                             xfermode, indices, indexCount, paint);
    }

    paint.reset();
    sk_free(vertices);
    sk_free(colors);
    SkSafeUnref(xfermode);
    sk_free(texs);
    sk_free(indices);
}

static void drawText_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    void *text = command->fData;
    SkPaint& paint = command->fPaint;

    if (!skip) {
        size_t byteLength = command->fSize;
        SkScalar x = command->fX;
        SkScalar y = command->fY;

        canvas->drawText(text, byteLength, x, y, paint);
    }
    paint.reset();
    sk_free(text);
}

static void drawPosText_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    void *text = command->fData;
    SkPoint* pos = command->fPoints;
    SkPaint& paint = command->fPaint;

    if (!skip) {
        size_t byteLength = command->fSize;
        canvas->drawPosText(text, byteLength, pos, paint);
    }

    sk_free(pos);
    sk_free(text);
    paint.reset();
}

static void drawPosTextH_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    void *text = command->fData;
    SkScalar* xpos = command->fS;
    SkPaint& paint = command->fPaint;

    if (!skip) {
        size_t byteLength = command->fSize;
        SkScalar ypos = command->fY;

        canvas->drawPosTextH(text, byteLength, xpos, ypos, paint);
    }
    sk_free(xpos);
    sk_free(text);
    paint.reset();
}

static void drawTextOnPath_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    void *text = command->fData;
    SkPaint& paint = command->fPaint;
    SkPath* path = command->fPath;

    if (!skip) {
        size_t byteLength = command->fSize;
        bool validMatrix = command->fPtrFlags & SkRecordQueue::kFirst_PointerFlag;
        if (validMatrix) {
            SkMatrix& matrix = command->fMatrix;
            canvas->drawTextOnPath(text, byteLength, *path, &matrix, paint);
        }
        else
            canvas->drawTextOnPath(text, byteLength, *path, NULL, paint);
    }
    paint.reset();
    delete path;
}

static void drawBitmap_playback(SkCanvas *canvas, SkRecordQueue::SkCanvasRecordInfo *command, bool skip)
{
    SkBitmap& bitmap = command->fBitmap;
    bool validPaint = command->fPtrFlags & SkRecordQueue::kFirst_PointerFlag;
    if (!skip) {
        if (validPaint) {
            SkPaint& paint = command->fPaint;
            canvas->drawBitmap(bitmap, command->fX, command->fY, &paint);
            paint.reset();
        }
        else
            canvas->drawBitmap(bitmap, command->fX, command->fY, NULL);
    }
    else {
        if (validPaint)
            command->fPaint.reset();
    }
    bitmap.reset();
}

static void drawBitmapMatrix_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    SkBitmap& bitmap = command->fBitmap;
    bool validPaint = command->fPtrFlags & SkRecordQueue::kFirst_PointerFlag;

    if (!skip) {
        SkMatrix& matrix = command->fMatrix;
        if (validPaint) {
            SkPaint& paint = command->fPaint;
            canvas->drawBitmapMatrix(bitmap, matrix, &paint);
            paint.reset();
        }
        else
            canvas->drawBitmapMatrix(bitmap, matrix, NULL);
    }
    else {
        if (validPaint)
            command->fPaint.reset();
    }
    bitmap.reset();
}

static void drawBitmapNine_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    SkBitmap& bitmap = command->fBitmap;
    bool validPaint = command->fPtrFlags & SkRecordQueue::kFirst_PointerFlag;

    if (!skip) {
        SkIRect& irect = command->fIRect;
        SkRect& rect = command->fRect1;
        if (validPaint) {
            SkPaint& paint = command->fPaint;
            canvas->drawBitmapNine(bitmap, irect, rect, &paint);
            paint.reset();
        }
        else
            canvas->drawBitmapNine(bitmap, irect, rect, NULL);
    }
    else {
        if (validPaint)
            command->fPaint.reset();
    }
    bitmap.reset();
}

static void drawBitmapRectToRect_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    SkBitmap& bitmap = command->fBitmap;
    bool validPaint = command->fPtrFlags & SkRecordQueue::kSecond_PointerFlag;
    SkPaint& paint = command->fPaint;
    SkCanvas::DrawBitmapRectFlags flags = (SkCanvas::DrawBitmapRectFlags) (command->fFlags >> SkRecordQueue::kCanvas_DrawBitmapRectFlag);

    if (!skip) {
        SkRect& src = command->fRect1;
        SkRect& dst = command->fRect2;
        bool validSrc = command->fPtrFlags & SkRecordQueue::kFirst_PointerFlag;

        if (validPaint & validSrc) {
            canvas->drawBitmapRectToRect(bitmap, &src, dst, &paint, flags);
            paint.reset();
        }
        else if (validPaint) {
            canvas->drawBitmapRectToRect(bitmap, NULL, dst, &paint, flags);
            paint.reset();
        }
        else if (validSrc) {
            canvas->drawBitmapRectToRect(bitmap, &src, dst, NULL, flags);
        }
        else {
            canvas->drawBitmapRectToRect(bitmap, NULL, dst, NULL, flags);
        }
    }
    else {
        if (validPaint)
            paint.reset();
    }
    bitmap.reset();
}

static void drawSprite_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    SkBitmap& bitmap = command->fBitmap;
    bool validPaint = command->fPtrFlags & SkRecordQueue::kFirst_PointerFlag;
    SkPaint& paint = command->fPaint;

    if (!skip) {
        if (validPaint) {
            canvas->drawSprite(bitmap, command->fI, command->fJ, &paint);
            paint.reset();
        }
        else {
            canvas->drawSprite(bitmap, command->fI, command->fJ, NULL);
        }
    }
    else {
        if (validPaint)
            paint.reset();
    }
    bitmap.reset();
}

static void drawData_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    if (!skip)
        canvas->drawData(command->fData, command->fSize);
    sk_free(command->fData);
}

static void drawPicture_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    if (!skip)
        canvas->drawPicture(command->fPicture);
    SkSafeUnref(command->fPicture);
}

static void setAllowSoftClip_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    if (!skip)
        canvas->setAllowSoftClip(command->fBool);
}

static void setAllowSimplifyClip_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    if (!skip)
        canvas->setAllowSimplifyClip(command->fBool);
}

static void pushCull_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    if (!skip) {
        SkRect& cullRect = command->fRect1;
        canvas->pushCull(cullRect);
    }
}

static void popCull_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    if (!skip)
        canvas->popCull();
}

static void setDrawFilter_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    SkDrawFilter* filter = command->fDrawFilter;
    if (!skip)
        canvas->setDrawFilter(filter);

    SkSafeUnref(filter);
}

static void flush_playback(SkCanvas* canvas, SkRecordQueue::SkCanvasRecordInfo* command, bool skip)
{
    if (!skip)
        canvas->flush();
}

static const PlaybackProc gPlaybackTable[] = {
    clipPath_playback,
    clipRegion_playback,
    clipRect_playback,
    clipRRect_playback,
    concat_playback,
    drawBitmap_playback,
    drawBitmapMatrix_playback,
    drawBitmapNine_playback,
    drawBitmapRectToRect_playback,
    clear_playback,
    drawData_playback,
    drawDRRect_playback,
    drawOval_playback,
    drawPaint_playback,
    drawPath_playback,
    drawPicture_playback,
    drawPoints_playback,
    drawPosText_playback,
    drawPosTextH_playback,
    drawRect_playback,
    drawRRect_playback,
    drawSprite_playback,
    drawText_playback,
    drawTextOnPath_playback,
    drawVertices_playback,
    restore_playback,
    rotate_playback,
    save_playback,
    saveLayer_playback,
    scale_playback,
    setMatrix_playback,
    skew_playback,
    translate_playback,
    setAllowSoftClip_playback,
    setAllowSimplifyClip_playback,
    pushCull_playback,
    popCull_playback,
    setDrawFilter_playback,
    flush_playback
};

SkRecordQueue::SkRecordQueue(){
    this->init();
}

void SkRecordQueue::init() {
    fMaxRecordingCommands = kDefaultMaxRecordingCommands;
    fUsedCommands = 0;
    fCanvas = NULL;
    fSaveLayerCount = 0;
    fQueue = SkNEW_ARGS(SkDeque, (sizeof(SkCanvasRecordInfo), fMaxRecordingCommands));
    fLayerStack = SkNEW_ARGS(SkDeque, (sizeof(bool), 10));
    fCondVar2 = SkNEW(SkCondVar);
}

SkRecordQueue::~SkRecordQueue() {
    this->flushPendingCommands(kSilentPlayback_Mode);
    delete fQueue;
    delete fLayerStack;
    SkSafeUnref(fCanvas);
    SkDELETE(fCondVar2);
}

void SkRecordQueue::setMaxRecordingCommands(size_t numCommands) {
    fMaxRecordingCommands = numCommands;
}

void SkRecordQueue::enableIsfFlush(bool isfFlush) {
        fFlushed = isfFlush;
}

void SkRecordQueue::setPlaybackCanvas(SkCanvas* canvas)
{
    SkRefCnt_SafeAssign(fCanvas, canvas);
}

void SkRecordQueue::playback(RecordPlaybackMode mode)
{
    fCondVar2->lock();
    const PlaybackProc* table = gPlaybackTable;
    bool skip = mode == kSilentPlayback_Mode;
    skip = false;

    if (!fCanvas)
        skip = true;
    while (!fQueue->empty()) {
        SkCanvasRecordInfo *command = reinterpret_cast<SkCanvasRecordInfo *> (fQueue->front());
        table[command->fCanvasOp](fCanvas, command, skip);

        if(command->fCanvasOp == flushOp) {
        fFlushed = true ;
        fCondVar2->signal();
        }
        fQueue->pop_front();
    }
    fUsedCommands = 0;
    fCondVar2->unlock();
}

void SkRecordQueue::skipPendingCommands() {
    flushPendingCommands(kSilentPlayback_Mode);
}

bool SkRecordQueue::hasPendingCommands() {
    return fUsedCommands != 0;
}

void SkRecordQueue::flushPendingCommands(RecordPlaybackMode playbackMode) {
    if (fUsedCommands == 0) {
        return;
    }

    playback(playbackMode);
}

void SkRecordQueue::clear(SkColor color)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();
    info->fColor = color;
    info->fCanvasOp = clearOp;
    fUsedCommands++;
}

void SkRecordQueue::drawPaint(const SkPaint& paint)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();
    info->fPaint = paint;
    info->fCanvasOp = drawPaintOp;
    fUsedCommands++;

}

void SkRecordQueue::drawPoints(SkCanvas::PointMode mode, size_t count, const SkPoint pts[],
                               const SkPaint& paint)
{
    if (count <= 0)
        return;

    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();
    info->fPaint = paint;
    info->fFlags = mode << kCanvas_PointModeFlag;
    info->fSize = count;
    info->fPoints = (SkPoint *)sk_malloc_throw(sizeof(SkPoint) * count);
    memcpy(info->fPoints, pts, sizeof(SkPoint) * count);
    info->fCanvasOp = drawPointsOp;
    fUsedCommands++;

}

void SkRecordQueue::drawOval(const SkRect& rect, const SkPaint& paint)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();
    info->fPaint = paint;
    info->fRect1 = rect;
    info->fCanvasOp = drawOvalOp;
    fUsedCommands++;

}

void SkRecordQueue::drawRect(const SkRect& oval, const SkPaint& paint)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();
    info->fPaint = paint;
    info->fRect1 = oval;
    info->fCanvasOp = drawRectOp;
    fUsedCommands++;

}

void SkRecordQueue::drawRRect(const SkRRect& rrect, const SkPaint& paint)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();
    info->fPaint = paint;
    info->fRRect1 = rrect;
    info->fCanvasOp = drawRRectOp;
    fUsedCommands++;

}

void SkRecordQueue::drawDRRect(const SkRRect& outer, const SkRRect& inner, const SkPaint& paint)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();
    info->fPaint = paint;
    info->fRRect1 = outer;
    info->fRRect2 = inner;
    info->fCanvasOp = drawDRRectOp;
    fUsedCommands++;

}

void SkRecordQueue::drawPath(const SkPath& path, const SkPaint& paint)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();
    info->fPaint = paint;
    info->fPath = SkNEW(SkPath);
    *(info->fPath) = path;
    info->fCanvasOp = drawPathOp;
    fUsedCommands++;

}

void SkRecordQueue::drawBitmap(const SkBitmap& bitmap, SkScalar left, SkScalar top,
                               const SkPaint* paint)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();
    if (paint) {
        info->fPaint = *paint;
        info->fPtrFlags = kFirst_PointerFlag;
    }
    else
        info->fPtrFlags = 0;
    info->fBitmap = bitmap;
    info->fX = left;
    info->fY = top;
    info->fCanvasOp = drawBitmapOp;
    fUsedCommands++;

}

void SkRecordQueue::drawBitmapRectToRect(const SkBitmap& bitmap, const SkRect* src,
                                         const SkRect& dst, const SkPaint* paint,
                                         SkCanvas::DrawBitmapRectFlags flags)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();
    if (paint) {
        info->fPaint = *paint;
        info->fPtrFlags = kSecond_PointerFlag;
    }
    else
        info->fPtrFlags = 0;

    if (src) {
        info->fRect1 = *src;
        info->fPtrFlags |= kFirst_PointerFlag;
    }

    info->fBitmap = bitmap;
    info->fRect2 = dst;
    info->fCanvasOp = drawBitmapRectToRectOp;
    info->fFlags = flags << kCanvas_DrawBitmapRectFlag;
    fUsedCommands++;

}

void SkRecordQueue::drawBitmapMatrix(const SkBitmap& bitmap, const SkMatrix& matrix, const SkPaint* paint)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    info->fBitmap = bitmap;
    info->fMatrix = matrix;
    info->fCanvasOp = drawBitmapMatrixOp;

    if (paint) {
        info->fPaint = *paint;
        info->fPtrFlags = kFirst_PointerFlag;
    }
    else
        info->fPtrFlags = 0;
    fUsedCommands++;

}

void SkRecordQueue::drawBitmapNine(const SkBitmap& bitmap, const SkIRect& center,
                        const SkRect& dst, const SkPaint* paint)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    info->fBitmap = bitmap;
    info->fIRect = center;
    info->fRect1 = dst;

    if (paint) {
        info->fPtrFlags = kFirst_PointerFlag;
        info->fPaint = *paint;
    }
    else
        info->fPtrFlags = 0;

    info->fCanvasOp = drawBitmapNineOp;
    fUsedCommands++;

}

void SkRecordQueue::drawSprite(const SkBitmap& bitmap, int left, int top, const SkPaint* paint)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    info->fBitmap = bitmap;
    info->fI = left;
    info->fJ = top;

    if (paint) {
        info->fPtrFlags = kFirst_PointerFlag;
        info->fPaint = *paint;
    }
    else
        info->fPtrFlags = 0;

    info->fCanvasOp = drawSpriteOp;
    fUsedCommands++;

}

void SkRecordQueue::drawVertices(SkCanvas::VertexMode vertexMode, int vertexCount,
                                 const SkPoint vertices[], const SkPoint texs[],
                                 const SkColor colors[], SkXfermode* mode,
                                 const uint16_t indices[], int indexCount,
                                 const SkPaint& paint)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    info->fPaint = paint;

    if (vertices) {
        info->fPoints = (SkPoint *)sk_malloc_throw(sizeof(SkPoint) * vertexCount);
        memcpy(info->fPoints, vertices, sizeof(SkPoint) * vertexCount);
    } else
        info->fPoints = NULL;

    info->fFlags = vertexMode << kCanvas_VertexModeFlag;

    if (colors) {
        info->fColors = (SkColor *)sk_malloc_throw(sizeof(SkColor) * vertexCount);
        memcpy(info->fColors, colors, sizeof(SkColor) * vertexCount);
    } else
        info->fColors = NULL;

    if (texs) {
        info->fTexs = (SkPoint *)sk_malloc_throw(sizeof(SkPoint) * vertexCount);
        memcpy(info->fTexs, texs, sizeof(SkPoint) * vertexCount);
    } else
        info->fTexs = NULL;

    if (indices) {
        info->fIndices = (uint16_t *)sk_malloc_throw(sizeof(uint16_t) * indexCount);
        memcpy(info->fIndices, indices, sizeof(uint16_t) * indexCount);
    } else
        info->fIndices = NULL;

    SkSafeRef(mode);
    info->fXfermode = mode;

    info->fI = vertexCount;
    info->fJ = indexCount;

    info->fCanvasOp = drawVerticesOp;
    fUsedCommands++;

}

void SkRecordQueue::drawData(const void* data, size_t size)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    if (!data) {
        info->fData = (void *)sk_malloc_throw(sizeof(char) * size);
        memcpy(info->fData, data, sizeof(char) * size);
    } else
        info->fData = NULL;
    info->fSize = size;

    info->fCanvasOp = drawDataOp;
    fUsedCommands++;

}

void SkRecordQueue::clipPath(const SkPath& path, SkRegion::Op op, bool doAntialias)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    info->fPath = SkNEW(SkPath);
    *(info->fPath) = path;
    info->fFlags = op << kCanvas_RegionOpFlag;
    info->fBool = doAntialias;
    info->fCanvasOp = clipPathOp;
    fUsedCommands++;

}

void SkRecordQueue::clipRegion(const SkRegion& deviceRgn, SkRegion::Op op)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    info->fRegion = deviceRgn;
    info->fFlags = op << kCanvas_RegionOpFlag;
    info->fCanvasOp = clipRegionOp;
    fUsedCommands++;

}

void SkRecordQueue::clipRect(const SkRect& rect, SkRegion::Op op, bool doAntialias)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    info->fRect1 = rect;
    info->fFlags = op << kCanvas_RegionOpFlag;
    info->fBool = doAntialias;
    info->fCanvasOp = clipRectOp;
    fUsedCommands++;

}

void SkRecordQueue::clipRRect(const SkRRect& rrect, SkRegion::Op op, bool doAntialias)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    info->fRRect1 = rrect;
    info->fFlags = op << kCanvas_RegionOpFlag;
    info->fBool = doAntialias;
    info->fCanvasOp = clipRRectOp;
    fUsedCommands++;

}

void SkRecordQueue::setMatrix(const SkMatrix& matrix)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    info->fMatrix = matrix;
    info->fCanvasOp = setMatrixOp;
    fUsedCommands++;

}

void SkRecordQueue::concat(const SkMatrix& matrix)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    info->fMatrix = matrix;
    info->fCanvasOp = concatOp;
    fUsedCommands++;

}

void SkRecordQueue::scale(SkScalar sx, SkScalar sy)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    info->fX = sx;
    info->fY = sy;
    info->fCanvasOp = scaleOp;
    fUsedCommands++;

}

void SkRecordQueue::skew(SkScalar sx, SkScalar sy)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    info->fX = sx;
    info->fY = sy;
    info->fCanvasOp = skewOp;
    fUsedCommands++;

}

void SkRecordQueue::rotate(SkScalar degrees)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    info->fX = degrees;
    info->fCanvasOp = rotateOp;
    fUsedCommands++;

}

void SkRecordQueue::translate(SkScalar dx, SkScalar dy)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    info->fX = dx;
    info->fY = dy;
    info->fCanvasOp = translateOp;
    fUsedCommands++;

}

void SkRecordQueue::save(SkCanvas::SaveFlags flags)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    info->fFlags = flags << kCanvas_SaveFlag;
    info->fCanvasOp = saveOp;
    fUsedCommands++;
    bool *saveLayer = reinterpret_cast<bool *> (fLayerStack->push_back());
    *saveLayer = false;

}

void SkRecordQueue::saveLayer(const SkRect* bounds, const SkPaint* paint, SkCanvas::SaveFlags flags)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    if (bounds) {
        info->fPtrFlags = kFirst_PointerFlag;
        info->fRect1 = *bounds;
    }
    else
        info->fPtrFlags = 0;

    if (paint) {
        info->fPtrFlags |= kSecond_PointerFlag;
        info->fPaint = *paint;
    }

    info->fFlags = flags << kCanvas_SaveFlag;
    info->fCanvasOp = saveLayerOp;
    fUsedCommands++;
    bool *saveLayer = reinterpret_cast<bool *> (fLayerStack->push_back());
    *saveLayer = true;
    fSaveLayerCount++;

}

void SkRecordQueue::restore()
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    info->fCanvasOp = restoreOp;
    fUsedCommands++;
    bool *saveLayer = reinterpret_cast<bool *> (fLayerStack->back());
    if (*saveLayer == true)
        fSaveLayerCount--;
    fLayerStack->pop_back();

}

void SkRecordQueue::pushCull(const SkRect& cullRect)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    info->fRect1 = cullRect;
    info->fCanvasOp = pushCullOp;
    fUsedCommands++;

}

void SkRecordQueue::popCull()
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    info->fCanvasOp = popCullOp;
    fUsedCommands++;

}

SkDrawFilter* SkRecordQueue::setDrawFilter(SkDrawFilter* filter)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    info->fCanvasOp = setDrawFilterOp;
    info->fDrawFilter = SkSafeRef(filter);
    fUsedCommands++;

    return filter;
}

void SkRecordQueue::setAllowSoftClip(bool allow)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    info->fCanvasOp = setAllowSoftClipOp;
    info->fBool = allow;
    fUsedCommands++;

}

void SkRecordQueue::setAllowSimplifyClip(bool allow)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    info->fCanvasOp = setAllowSimplifyClipOp;
    info->fBool = allow;
    fUsedCommands++;

}

void SkRecordQueue::drawText(const void* text, size_t byteLength,
                             SkScalar x, SkScalar y, const SkPaint& paint)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    info->fCanvasOp = drawTextOp;
    info->fPaint = paint;
    info->fX = x;
    info->fY = y;
    info->fSize = byteLength;

    info->fData = (void*) sk_malloc_throw(sizeof(char) * byteLength);
    memcpy(info->fData, text, sizeof(char) * byteLength);

    fUsedCommands++;

}

void SkRecordQueue::drawPosText(const void* text, size_t byteLength,
                               const SkPoint pos[], const SkPaint& paint)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    int count = paint.textToGlyphs(text, byteLength, NULL);
    info->fCanvasOp = drawPosTextOp;
    info->fPaint = paint;

    info->fPoints = (SkPoint*) sk_malloc_throw(sizeof(SkPoint) * count);
    memcpy(info->fPoints, pos, sizeof(SkPoint) * count);
    info->fSize = byteLength;

    info->fData = (void*) sk_malloc_throw(sizeof(char) * byteLength);
    memcpy(info->fData, text, sizeof(char) * byteLength);

    fUsedCommands++;

}

void SkRecordQueue::drawPosTextH(const void* text, size_t byteLength,
                                const SkScalar xpos[], SkScalar constY,
                                const SkPaint& paint)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    int count = paint.textToGlyphs(text, byteLength, NULL);
    info->fCanvasOp = drawPosTextHOp;
    info->fPaint = paint;

    info->fS = (SkScalar*) sk_malloc_throw(sizeof(SkScalar) * count);
    memcpy(info->fS, xpos, sizeof(SkPoint) * count);
    info->fY = constY;
    info->fSize = byteLength;

    info->fData = (void*) sk_malloc_throw(sizeof(char) * byteLength);
    memcpy(info->fData, text, sizeof(char) * byteLength);

    fUsedCommands++;

}

void SkRecordQueue::drawPicture(const SkPicture *picture)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();
    SkSafeRef(picture);
    info->fPicture = (SkPicture*)(picture);
    info->fCanvasOp = drawPictureOp;
    fUsedCommands++;

}

void SkRecordQueue::flush()
{
    fCondVar2->lock();
    SkCanvasRecordInfo *info = createRecordInfo();
    info->fCanvasOp = flushOp;
    fUsedCommands++;

    if(!fFlushed) {
    fCondVar2->wait();
    }

    fCondVar2->unlock();

}

void SkRecordQueue::drawTextOnPath(const void* text, size_t byteLength,
                                  const SkPath& path, const SkMatrix* matrix,
                                  const SkPaint& paint)
{
    if (fUsedCommands == fMaxRecordingCommands)
        playback(kNormalPlayback_Mode);

    SkAutoMutexAcquire lock(fCondVar1);
    SkCanvasRecordInfo *info = createRecordInfo();

    int count = paint.textToGlyphs(text, byteLength, NULL);
    info->fCanvasOp = drawTextOnPathOp;
    info->fPaint = paint;

    if (matrix) {
        info->fPtrFlags = kFirst_PointerFlag;
        info->fMatrix = *matrix;
    }
    else
        info->fPtrFlags = 0;

    info->fPath = SkNEW(SkPath);
    *(info->fPath) = path;
    info->fSize = byteLength;

    info->fData = (void*) sk_malloc_throw(sizeof(char) * byteLength);
    memcpy(info->fData, text, sizeof(char) * byteLength);

    fUsedCommands++;
}

SkRecordQueue::SkCanvasRecordInfo*  SkRecordQueue::createRecordInfo()
{
    SkCanvasRecordInfo *info = reinterpret_cast<SkCanvasRecordInfo *> (fQueue->push_back());
    memset(info, 0, sizeof(SkCanvasRecordInfo));
    fFlushed = false;
    return info;
}
