/*
 * Copyright 2014 Samsung Research America, Inc - Silicon Valley
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkRecordQueue_DEFINED
#define SkRecordQueue_DEFINED

#include "SkDrawFilter.h"
#include "SkPaint.h"
#include "SkDeque.h"
#include "SkRect.h"
#include "SkRRect.h"
#include "SkRegion.h"
#include "SkBitmap.h"
#include "SkPath.h"
#include "SkCanvas.h"
#include "SkPoint.h"
#include "SkMatrix.h"
#include "SkPicture.h"
#include "SkTextBlob.h"

class SkRecordQueue {
public:
    enum RecordPlaybackMode {
        kNormalPlayback_Mode,
        kSilentPlayback_Mode,
    };

    enum CanvasFlags {
        kCanvas_SaveFlag = 24,
        kCanvas_VertexModeFlag = 20,
        kCanvas_PointModeFlag = 16,
        kCanvas_RegionOpFlag = 8,
        kCanvas_DrawBitmapRectFlag = 0,
    };

    enum ValidPointerFlags {
        kFirst_PointerFlag = 0x1,
        kSecond_PointerFlag = 0x2,
        kThird_PointerFlag = 0x4,
        kFourth_PointerFlag = 0x8,
    };

    enum CanvasOps {
        clipPathOp  = 0,
        clipRegionOp,
        clipRectOp,
        clipRRectOp,
        concatOp,
        drawBitmapOp,
        drawBitmapMatrixOp,
        drawBitmapNineOp,
        drawBitmapRectToRectOp,
        clearOp,
        drawDRRectOp,
        drawOvalOp,
        drawPaintOp,
        drawPatchOp,
        drawPathOp,
        drawPictureOp,
        drawPointsOp,
        drawPosTextOp,
        drawPosTextHOp,
        drawRectOp,
        drawRRectOp,
        drawSpriteOp,
        drawTextOp,
        drawTextBlobOp,
        drawTextOnPathOp,
        drawVerticesOp,
        restoreOp,
        saveOp,
        saveLayerOp,
        setMatrixOp,
        setDrawFilterOp
    };

    struct SkCanvasRecordInfo {
        bool fSkip;
        CanvasOps fCanvasOp;
        SkPaint fPaint;
        uint8_t fPtrFlags;
        uint32_t fFlags;            // SkCanvas Save flags, also used
                                    // for passing xfermode, SkRegion::Op
                                    // PointMode, DrawBitmapRectFlags
        SkRect fRect1;              // first SkRect used in calls
        SkRect fRect2;              // second SkRect used in calls
        SkRRect fRRect1;            // first SkRRect used in calls
        SkRRect fRRect2;            // second SkRRect used in calls
        SkIRect fIRect;             // SkIRrect used in call
        SkRegion fRegion;           // region used in call
        SkMatrix fMatrix;           // matrix used as parameter
        bool fBool;                 // used for doAntialias, allowSoftClip,
                                    // allowSimplifyClip, useCenter,
        SkScalar fX;
        SkScalar fY;
        SkScalar* fS;
        SkPath*  fPath;                // used for passing SkPath
        SkColor fColor;              // for single constant color;
        SkColor* fColors;            // array of color
        SkDrawFilter* fDrawFilter;
        SkBitmap fBitmap;

        int fI;                       // used for drawSprite, vertexCount 
                                      // in drawVertices
        int fJ;                       // used for drawSprite, and indexCount
                                      // in drawVertices
        size_t fSize;                 // used in drawPoints, drawText,
                                      // drawTextH, drawPosText,
                                      // drawTextOnPathHW, and drawTextOnPath
        SkPoint*  fPoints;            // used for drawPoints, drawPosText,
                                      // drawVetices
        SkPoint* fTexs;               // used for drawVertices
        uint16_t* fIndices;
        SkXfermode* fXfermode;
        void* fData;                    // used for drawText and drawData;
        SkPicture* fPicture;
        SkTextBlob* fTextBlob;
    };

    SkRecordQueue();
    ~SkRecordQueue();

    void markPendingCommandsForSilentFlush();
    void setPlaybackCanvas(SkCanvas* canvas);
    bool hasPendingCommands();
    void flushPendingCommands(RecordPlaybackMode mode, bool threaded);
    void setMaxRecordingCommands(size_t numCommands);
    void enableThreadSafePlayback();
    void prepareForThreadSafePlayback();
    size_t storageAllocatedForRecordingCommands();

    // draw commands from SkCanvas
    bool isDrawingToLayer() { return fSaveLayerCount > 0; }
    void clear(SkColor);
    void drawPaint(const SkPaint& paint);
    void drawPoints(SkCanvas::PointMode, size_t count, const SkPoint pts[],
                    const SkPaint&);
    void drawOval(const SkRect&, const SkPaint&);
    void drawRect(const SkRect&, const SkPaint&);
    void drawRRect(const SkRRect&, const SkPaint&);
    void drawDRRect(const SkRRect&, const SkRRect&, const SkPaint&);
    void drawPath(const SkPath&, const SkPaint&);
    void drawPatch(const SkPoint cubics[12], const SkColor colors[4],
                   const SkPoint texCoords[4], SkXfermode* xmode,
                   const SkPaint& paint);
    void drawBitmap(const SkBitmap&, SkScalar left, SkScalar top,
                    const SkPaint*);
    void drawBitmapRectToRect(const SkBitmap&, const SkRect* src,
                              const SkRect& dst, const SkPaint* paint,
                              SkCanvas::DrawBitmapRectFlags flags);
    void drawBitmapMatrix(const SkBitmap&, const SkMatrix&, const SkPaint*);
    void drawBitmapNine(const SkBitmap&, const SkIRect& center,
                        const SkRect& dst, const SkPaint* paint = NULL);
    void drawSprite(const SkBitmap&, int left, int top, const SkPaint*);
    void drawText(const void* text, size_t byteLength, SkScalar x,
                  SkScalar y, const SkPaint& paint);
    void drawTextBlob(const SkTextBlob* blob, SkScalar x, SkScalar y,
                      const SkPaint& paint);
    void drawPosText(const void* text, size_t byteLength, const SkPoint pos[],
                     const SkPaint& paint);
    void drawPosTextH(const void* text, size_t byteLength, const SkScalar xpos[],
                      SkScalar constY, const SkPaint& paint);
    void drawTextOnPath(const void* text, size_t byteLength,
                        const SkPath& path, const SkMatrix* matrix,
                        const SkPaint& paint);
    void drawVertices(SkCanvas::VertexMode, int vertexCount,
                      const SkPoint vertices[], const SkPoint texs[],
                      const SkColor colors[], SkXfermode*,
                      const uint16_t indices[], int indexCount,
                      const SkPaint&);
    void drawData(const void*, size_t);
    void clipPath(const SkPath& path, SkRegion::Op op, bool doAntialias);
    void clipRegion(const SkRegion& deviceRgn, SkRegion::Op op);
    void clipRect(const SkRect& rect, SkRegion::Op op, bool doAntialias);
    void clipRRect(const SkRRect& rrect, SkRegion::Op op, bool doAntialias);
    void setMatrix(const SkMatrix& matrix);
    void concat(const SkMatrix& matrix);
    void save();
    void saveLayer(const SkRect* bounds, const SkPaint* paint, SkCanvas::SaveFlags flags);
    void restore();
    SkDrawFilter* setDrawFilter(SkDrawFilter* filter);
    void drawPicture(const SkPicture* picture, const SkMatrix* matrix,
                     const SkPaint* paint);

private:
    enum {
        // Deferred canvas will auto-flush when recording reaches this limit
        kDefaultMaxRecordingCommands = 8192,
        kDeferredCanvasBitmapSizeThreshold = ~0U, // Disables this feature
    };

    void init();
    void playback(RecordPlaybackMode playbackMode, bool threaded);
    SkCanvasRecordInfo* createRecordInfo();

    SkDeque*  fRecordQueue;
    SkDeque*  fPlaybackQueue;
    size_t    fMaxRecordingCommands;
    size_t    fUsedCommands;
    SkCanvas* fCanvas;
    size_t    fSaveLayerCount;
    SkDeque   *fLayerStack;
    bool      fThreadSafePlayback;
};

#endif

