/*
 * Copyright 2014 Samsung Research America, Inc - Silicon Valley
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkLightDeferredCanvas.h"
#include "SkRecordQueue.h"

#include "SkBitmapDevice.h"
#include "SkChunkAlloc.h"
#include "SkColorFilter.h"
#include "SkDrawFilter.h"
#include "SkPaint.h"
#include "SkPaintPriv.h"
#include "SkRRect.h"
#include "SkShader.h"
#include "SkSurface.h"
#include "SkSurfaceProps.h"

enum {
    // Deferred canvas will auto-flush when recording reaches this limit
    kDefaultMaxRecordingCommands = 8196,
    kDeferredCanvasBitmapSizeThreshold = ~0U, // Disables this feature
};

enum PlaybackMode {
    kNormal_PlaybackMode,
    kSilent_PlaybackMode,
};

static bool shouldDrawImmediately(const SkBitmap* bitmap, const SkPaint* paint,
                           size_t bitmapSizeThreshold) {
    if (bitmap && ((bitmap->getTexture() && !bitmap->isImmutable()) ||
        (bitmap->getSize() > bitmapSizeThreshold))) {
        return true;
    }
    if (paint) {
        SkShader* shader = paint->getShader();
        // Here we detect the case where the shader is an SkBitmapProcShader
        // with a gpu texture attached.  Checking this without RTTI
        // requires making the assumption that only gradient shaders
        // and SkBitmapProcShader implement asABitmap().  The following
        // code may need to be revised if that assumption is ever broken.
        if (shader && !shader->asAGradient(NULL)) {
            SkBitmap bm;
            if (shader->asABitmap(&bm, NULL, NULL) &&
                NULL != bm.getTexture()) {
                return true;
            }
        }
    }
    return false;
}

//-----------------------------------------------------------------------------
// SkLightDeferredDevice
//-----------------------------------------------------------------------------
class SkLightDeferredDevice : public SkBaseDevice {
public:
    explicit SkLightDeferredDevice(SkSurface* surface);
    ~SkLightDeferredDevice();

    void setNotificationClient(SkLightDeferredCanvas::NotificationClient* notificationClient);
    SkCanvas* immediateCanvas() const {return fImmediateCanvas;}
    SkRecordQueue& recorder()  { return fRecorder; }
    SkBaseDevice* immediateDevice() const {return fImmediateCanvas->getTopDevice();}
    SkImage* newImageSnapshot();
    void setSurface(SkSurface* surface);
    bool isFreshFrame();
    bool hasPendingCommands();
    size_t commandsAllocatedForRecording() const;
    size_t getBitmapSizeThreshold() const;
    void setBitmapSizeThreshold(size_t sizeThreshold);
    void flushPendingCommands(SkRecordQueue::RecordPlaybackMode);
    void skipPendingCommands();
    void setMaxRecordingCommands(size_t);

    virtual int width() const SK_OVERRIDE;
    virtual int height() const SK_OVERRIDE;
#ifdef SK_SUPPORT_LEGACY_DEVICE_CONFIG
    virtual SkBitmap::Config config() const SK_OVERRIDE;
#endif
    virtual bool isOpaque() const SK_OVERRIDE;
    virtual SkImageInfo imageInfo() const SK_OVERRIDE;

    virtual GrRenderTarget* accessRenderTarget() SK_OVERRIDE;

    virtual SkBaseDevice* onCreateDevice(const SkImageInfo&, Usage) SK_OVERRIDE;

    virtual SkSurface* newSurface(const SkImageInfo&) SK_OVERRIDE;

    void enableThreadedPlayback(bool enable);
    void waitForCompletion();
    bool getOnCurrentThread() const { return fIsOnCurrentThread; }
    void setOnCurrentThread(bool isOnCurrentThread) { fIsOnCurrentThread = isOnCurrentThread; }

protected:
    virtual const SkBitmap& onAccessBitmap() SK_OVERRIDE;
    virtual bool onReadPixels(const SkImageInfo&, void*, size_t, int x, int y) SK_OVERRIDE;
    virtual bool onWritePixels(const SkImageInfo&, const void*, size_t, int x, int y) SK_OVERRIDE;

    // The following methods are no-ops on a deferred device
    virtual bool filterTextFlags(const SkPaint& paint, TextFlags*) SK_OVERRIDE {
        return false;
    }

    // None of the following drawing methods should ever get called on the
    // deferred device
    virtual void clear(SkColor color) SK_OVERRIDE
        {SkASSERT(0);}
    virtual void drawPaint(const SkDraw&, const SkPaint& paint) SK_OVERRIDE
        {SkASSERT(0);}
    virtual void drawPoints(const SkDraw&, SkCanvas::PointMode mode,
                            size_t count, const SkPoint[],
                            const SkPaint& paint) SK_OVERRIDE
        {SkASSERT(0);}
    virtual void drawRect(const SkDraw&, const SkRect& r,
                            const SkPaint& paint) SK_OVERRIDE
        {SkASSERT(0);}
    virtual void drawOval(const SkDraw&, const SkRect&, const SkPaint&) SK_OVERRIDE
        {SkASSERT(0);}
    virtual void drawRRect(const SkDraw&, const SkRRect& rr,
                           const SkPaint& paint) SK_OVERRIDE
    {SkASSERT(0);}
    virtual void drawPath(const SkDraw&, const SkPath& path,
                            const SkPaint& paint,
                            const SkMatrix* prePathMatrix = NULL,
                            bool pathIsMutable = false) SK_OVERRIDE
        {SkASSERT(0);}
    virtual void drawBitmap(const SkDraw&, const SkBitmap& bitmap,
                            const SkMatrix& matrix, const SkPaint& paint) SK_OVERRIDE
        {SkASSERT(0);}
    virtual void drawBitmapRect(const SkDraw&, const SkBitmap&, const SkRect*,
                                const SkRect&, const SkPaint&,
                                SkCanvas::DrawBitmapRectFlags) SK_OVERRIDE
        {SkASSERT(0);}
    virtual void drawSprite(const SkDraw&, const SkBitmap& bitmap,
                            int x, int y, const SkPaint& paint) SK_OVERRIDE
        {SkASSERT(0);}
    virtual void drawText(const SkDraw&, const void* text, size_t len,
                            SkScalar x, SkScalar y, const SkPaint& paint) SK_OVERRIDE
        {SkASSERT(0);}
    virtual void drawPosText(const SkDraw&, const void* text, size_t len,
                                const SkScalar pos[], int scalarsPerPos,                       
                                const SkPoint& offset, const SkPaint& paint) SK_OVERRIDE
        {SkASSERT(0);}
    virtual void drawTextOnPath(const SkDraw&, const void* text,
                                size_t len, const SkPath& path,
                                const SkMatrix* matrix,
                                const SkPaint& paint) SK_OVERRIDE
        {SkASSERT(0);}
    virtual void drawVertices(const SkDraw&, SkCanvas::VertexMode,
                                int vertexCount, const SkPoint verts[],
                                const SkPoint texs[], const SkColor colors[],
                                SkXfermode* xmode, const uint16_t indices[],
                                int indexCount, const SkPaint& paint) SK_OVERRIDE
        {SkASSERT(0);}
    virtual void drawDevice(const SkDraw&, SkBaseDevice*, int x, int y,
                            const SkPaint&) SK_OVERRIDE
        {SkASSERT(0);}

    virtual void lockPixels() SK_OVERRIDE {}
    virtual void unlockPixels() SK_OVERRIDE {}

    virtual bool allowImageFilter(const SkImageFilter*) SK_OVERRIDE {
        return false;
    }
    virtual bool canHandleImageFilter(const SkImageFilter*) SK_OVERRIDE {
        return false;
    }
    virtual bool filterImage(const SkImageFilter*, const SkBitmap&,
                             const SkImageFilter::Context&, SkBitmap*, SkIPoint*) SK_OVERRIDE {
        return false;
    }


private:
    void shutdown();

    virtual void flush() SK_OVERRIDE;
    virtual void replaceBitmapBackendForRasterSurface(const SkBitmap&) SK_OVERRIDE {}

    void init();
    void aboutToDraw();
    void prepareForImmediatePixelWrite();

    SkRecordQueue fRecorder;
    SkCanvas* fImmediateCanvas;
    SkSurface* fSurface;
    SkLightDeferredCanvas::NotificationClient* fNotificationClient;
    bool fFreshFrame;
    bool fCanDiscardCanvasContents;
    bool fIsThreadedPlayback;
    bool fIsOnCurrentThread;
    size_t fMaxRecordingCommands;
    size_t fPreviousCommandsAllocated;
    size_t fBitmapSizeThreshold;
};

SkLightDeferredDevice::SkLightDeferredDevice(SkSurface* surface) {
    fMaxRecordingCommands = kDefaultMaxRecordingCommands;
    fNotificationClient = NULL;
    fImmediateCanvas = NULL;
    fSurface = NULL;
    this->setSurface(surface);
    this->init();
}

void SkLightDeferredDevice::setSurface(SkSurface* surface) {
    SkRefCnt_SafeAssign(fImmediateCanvas, surface->getCanvas());
    SkRefCnt_SafeAssign(fSurface, surface);
    fRecorder.setPlaybackCanvas(fImmediateCanvas);
    fIsThreadedPlayback = false;

    fRecorder.setSurface(fSurface);
}

void SkLightDeferredDevice::init() {
    fFreshFrame = true;
    fCanDiscardCanvasContents = false;
    fPreviousCommandsAllocated = 0;
    fBitmapSizeThreshold = kDeferredCanvasBitmapSizeThreshold;
    fMaxRecordingCommands = kDefaultMaxRecordingCommands;
    fNotificationClient = NULL;
    fIsOnCurrentThread = true;
}

SkLightDeferredDevice::~SkLightDeferredDevice() {
    this->flushPendingCommands(SkRecordQueue::kSilentPlayback_Mode);
    this->shutdown();
    SkSafeUnref(fImmediateCanvas);
    SkSafeUnref(fSurface);
}

void SkLightDeferredDevice::setMaxRecordingCommands(size_t maxCommands) {
    fMaxRecordingCommands = maxCommands;
    fRecorder.setMaxRecordingCommands(maxCommands);
}

void SkLightDeferredDevice::setNotificationClient(
    SkLightDeferredCanvas::NotificationClient* notificationClient) {
    fNotificationClient = notificationClient;
    fRecorder.setNotificationClient(notificationClient);
}

void SkLightDeferredDevice::skipPendingCommands() {
    if (!fRecorder.isDrawingToLayer()) {
        fCanDiscardCanvasContents = true;

        if (fIsThreadedPlayback)
            fCanDiscardCanvasContents = false;

        if (fRecorder.hasPendingCommands()) {
            fFreshFrame = true;
            flushPendingCommands(SkRecordQueue::kSilentPlayback_Mode);
        }
        if (fNotificationClient) {
            // This is nasty.  If playback is on another thread,
            // we assume that notification client must be called
            // on this another thread
            if (fIsThreadedPlayback)
                fRecorder.notifyClientForSkippedPendingDrawCommands();
            else
                fNotificationClient->skippedPendingDrawCommands();
        }
    }
}

bool SkLightDeferredDevice::isFreshFrame() {
    bool ret = fFreshFrame;
    fFreshFrame = false;
    return ret;
}

bool SkLightDeferredDevice::hasPendingCommands() {
    return fRecorder.hasPendingCommands();
}

void SkLightDeferredDevice::aboutToDraw()
{
    if (fCanDiscardCanvasContents) {
        if (NULL != fSurface) {
            if (fIsThreadedPlayback)
                fRecorder.notifySurfaceForContentWillChange(SkSurface::kDiscard_ContentChangeMode);
            else
                fSurface->notifyContentWillChange(SkSurface::kDiscard_ContentChangeMode);
        }
        fCanDiscardCanvasContents = false;
    }
}

void SkLightDeferredDevice::flushPendingCommands(SkRecordQueue::RecordPlaybackMode playbackMode) {
    if (!fRecorder.hasPendingCommands()) {
        return;
    }
    if (playbackMode == SkRecordQueue::kNormalPlayback_Mode) {
        aboutToDraw();
    }
    fRecorder.flushPendingCommands(playbackMode);

    if (fNotificationClient) {
        if (fIsThreadedPlayback) {
            if (playbackMode == SkRecordQueue::kSilentPlayback_Mode) {
                fRecorder.notifyClientForSkippedPendingDrawCommands();
            } else {
                fRecorder.notifyClientForFlushedDrawCommands();
            }
        } else {
            if (playbackMode == SkRecordQueue::kSilentPlayback_Mode) {
                fNotificationClient->skippedPendingDrawCommands();
            } else {
                fNotificationClient->flushedDrawCommands();
            }
        }
    }
}

void SkLightDeferredDevice::flush() {
    this->flushPendingCommands(SkRecordQueue::kNormalPlayback_Mode);
    fRecorder.flush();
    if (!fIsThreadedPlayback)
        fImmediateCanvas->flush();
}

void SkLightDeferredDevice::waitForCompletion()
{
    if (fIsThreadedPlayback)
        fRecorder.wait();
}

void SkLightDeferredDevice::shutdown()
{
    if (fIsThreadedPlayback) {
        fRecorder.flush();
        fRecorder.notifyClientForFinishDraw();
        fRecorder.waitForPlaybackToJoin();
    }

    fIsThreadedPlayback = false;
    if (fNotificationClient)
        fNotificationClient->prepareForDraw();
    fIsOnCurrentThread = true;
}

size_t SkLightDeferredDevice::getBitmapSizeThreshold() const {
    return fBitmapSizeThreshold;
}

void SkLightDeferredDevice::setBitmapSizeThreshold(size_t sizeThreshold) {
    fBitmapSizeThreshold = sizeThreshold;
}

SkImage* SkLightDeferredDevice::newImageSnapshot() {
    // we need to flush any pending commands, wait for completion of 
    // these commands.
    flush();

    // we must tell playback thread to switch out context
    if (fIsThreadedPlayback && !fIsOnCurrentThread)
        fRecorder.notifyClientForFinishDraw();
    // wait for all commands to be flushed
    waitForCompletion();
    fIsOnCurrentThread = true;

    if (fNotificationClient)
        fNotificationClient->prepareForDraw();

    return fSurface ? fSurface->newImageSnapshot() : NULL;
}

int SkLightDeferredDevice::width() const {
    return immediateDevice()->width();
}

int SkLightDeferredDevice::height() const {
    return immediateDevice()->height();
}

#ifdef SK_SUPPORT_LEGACY_DEVICE_CONFIG
SkBitmap::Config SkLightDeferredDevice::config() const {
    return immediateDevice()->config();
}
#endif

bool SkLightDeferredDevice::isOpaque() const {
    return immediateDevice()->isOpaque();
}

SkImageInfo SkLightDeferredDevice::imageInfo() const {
    return immediateDevice()->imageInfo();
}

GrRenderTarget* SkLightDeferredDevice::accessRenderTarget() {
    this->flushPendingCommands(SkRecordQueue::kNormalPlayback_Mode);
    this->waitForCompletion();

    return immediateDevice()->accessRenderTarget();
}

void SkLightDeferredDevice::prepareForImmediatePixelWrite() {
    // The purpose of the following code is to make sure commands are flushed, that
    // aboutToDraw() is called and that notifyContentWillChange is called, without
    // calling anything redundantly.
    if (fRecorder.hasPendingCommands()) {
        this->flushPendingCommands(SkRecordQueue::kNormalPlayback_Mode);
    } else {
        bool mustNotifyDirectly = !fCanDiscardCanvasContents;
        this->aboutToDraw();
        if (mustNotifyDirectly) {
            if (fIsThreadedPlayback)
                fRecorder.notifySurfaceForContentWillChange(SkSurface::kRetain_ContentChangeMode);
            else
                fSurface->notifyContentWillChange(SkSurface::kRetain_ContentChangeMode);
        }
    }

    if (fIsThreadedPlayback) {
        flush();
        if (fNotificationClient)
            this->recorder().notifyClientForFinishDraw();
        waitForCompletion();

        if (fNotificationClient)
            fNotificationClient->prepareForDraw();
    } else
        fImmediateCanvas->flush();

    fIsOnCurrentThread = true;
}

bool SkLightDeferredDevice::onWritePixels(const SkImageInfo& info, const void* pixels, size_t rowBytes,
                                   int x, int y) {
    SkASSERT(x >= 0 && y >= 0);
    SkASSERT(x + info.width() <= width());
    SkASSERT(y + info.height() <= height());

    this->flushPendingCommands(SkRecordQueue::kNormalPlayback_Mode);

    const SkImageInfo deviceInfo = this->imageInfo();
    if (info.width() == deviceInfo.width() && info.height() == deviceInfo.height()) {
        this->skipPendingCommands();
    }

    this->prepareForImmediatePixelWrite();
    return immediateDevice()->onWritePixels(info, pixels, rowBytes, x, y);
}

const SkBitmap& SkLightDeferredDevice::onAccessBitmap() {
    this->flushPendingCommands(SkRecordQueue::kNormalPlayback_Mode);
    if (fIsThreadedPlayback) {
        this->flush();
        if (fNotificationClient)
            this->recorder().notifyClientForFinishDraw();
        this->waitForCompletion();

        if (fNotificationClient)
            fNotificationClient->prepareForDraw();
    }
    fIsOnCurrentThread = true;
    return immediateDevice()->accessBitmap(false);
}

SkBaseDevice* SkLightDeferredDevice::onCreateDevice(const SkImageInfo& info, Usage usage) {
    // Save layer usage not supported, and not required by SkDeferredCanvas.
    SkASSERT(usage != kSaveLayer_Usage);
    // Create a compatible non-deferred device.
    // We do not create a deferred device because we know the new device
    // will not be used with a deferred canvas (there is no API for that).
    // And connecting a SkDeferredDevice to non-deferred canvas can result
    // in unpredictable behavior.
    return immediateDevice()->createCompatibleDevice(info);
}

SkSurface* SkLightDeferredDevice::newSurface(const SkImageInfo& info) {
    return this->immediateDevice()->newSurface(info,SkSurfaceProps(SkSurfaceProps::kUseDistanceFieldFonts_Flag,SkSurfaceProps::kLegacyFontHost_InitType));
}

bool SkLightDeferredDevice::onReadPixels(const SkImageInfo& info, void* pixels, size_t rowBytes,
                                    int x, int y) {
    this->flushPendingCommands(SkRecordQueue::kNormalPlayback_Mode);

    if (fNotificationClient) {
        this->flush();
        if (fNotificationClient)
            this->recorder().notifyClientForFinishDraw();
        fNotificationClient->prepareForDraw();
    }
    return fImmediateCanvas->readPixels(info, pixels, rowBytes, x, y);
}

void SkLightDeferredDevice::enableThreadedPlayback(bool enable) {
    if (fIsThreadedPlayback != enable) {
        flushPendingCommands(SkRecordQueue::kNormalPlayback_Mode);
        flush();
        if (fIsThreadedPlayback) {
            // context switch
            if (!fIsOnCurrentThread)
                fRecorder.notifyClientForFinishDraw();
                fIsOnCurrentThread = true;

            waitForCompletion();
            fRecorder.waitForPlaybackToJoin();
        }

        if (fNotificationClient && enable == false)
            fNotificationClient->prepareForDraw();

        fIsThreadedPlayback = enable;
        fRecorder.enableThreadedPlayback(enable);
    }
}

class LightAutoImmediateDrawIfNeeded {
public:
    LightAutoImmediateDrawIfNeeded(SkLightDeferredCanvas& canvas, const SkBitmap* bitmap,
                              const SkPaint* paint) {
        this->init(canvas, bitmap, paint);
    }

    LightAutoImmediateDrawIfNeeded(SkLightDeferredCanvas& canvas, const SkPaint* paint) {
        this->init(canvas, NULL, paint);
    }

    ~LightAutoImmediateDrawIfNeeded() {
        if (fCanvas) {
            fCanvas->setDeferredDrawing(true);
        }
    }
private:
    void init(SkLightDeferredCanvas& canvas, const SkBitmap* bitmap, const SkPaint* paint)
    {
        SkLightDeferredDevice* device = canvas.getDeferredDevice();
        if (canvas.isDeferredDrawing() && (NULL != device) &&
            shouldDrawImmediately(bitmap, paint, device->getBitmapSizeThreshold())) {
            canvas.setDeferredDrawing(false);
            fCanvas = &canvas;
        } else {
            fCanvas = NULL;
        }
    }

    SkLightDeferredCanvas* fCanvas;
};

SkLightDeferredCanvas* SkLightDeferredCanvas::Create(SkSurface* surface) {
    SkAutoTUnref<SkLightDeferredDevice> deferredDevice(SkNEW_ARGS(SkLightDeferredDevice, (surface)));
    return SkNEW_ARGS(SkLightDeferredCanvas, (deferredDevice));
}

SkLightDeferredCanvas::SkLightDeferredCanvas(SkLightDeferredDevice* device) : SkCanvas (device) {
    this->init();
}

void SkLightDeferredCanvas::init() {
    fDeferredDrawing = true; // On by default
    fIsThreadedPlayback = false; // off by default
    fNotificationClient = NULL;
    fIsOnCurrentThread = true;
}

void SkLightDeferredCanvas::setMaxRecordingCommands(size_t maxCommands) {
    this->validate();
    this->getDeferredDevice()->setMaxRecordingCommands(maxCommands);
}

void SkLightDeferredCanvas::setBitmapSizeThreshold(size_t sizeThreshold) {
    SkLightDeferredDevice* deferredDevice = this->getDeferredDevice();
    SkASSERT(deferredDevice);
    deferredDevice->setBitmapSizeThreshold(sizeThreshold);
}

void SkLightDeferredCanvas::validate() const {
    SkASSERT(this->getDevice());
}

SkCanvas* SkLightDeferredCanvas::immediateCanvas() const {
    this->validate();
    return this->getDeferredDevice()->immediateCanvas();
}

SkLightDeferredDevice* SkLightDeferredCanvas::getDeferredDevice() const {
    return static_cast<SkLightDeferredDevice*>(this->getDevice());
}

void SkLightDeferredCanvas::setDeferredDrawing(bool val) {
    this->validate(); // Must set device before calling this method
    if (val != fDeferredDrawing) {
        if (fDeferredDrawing) {
            // Going live.
            if (fIsThreadedPlayback && !fIsOnCurrentThread) {
                if (fNotificationClient) {
                    flush();
                    this->getDeferredDevice()->recorder().notifyClientForFinishDraw();
                    this->getDeferredDevice()->waitForCompletion();
                    fNotificationClient->prepareForDraw();
                }
            }
            fIsOnCurrentThread = true;
            this->getDeferredDevice()->setOnCurrentThread(true);
        }
        fDeferredDrawing = val;

        if (fDeferredDrawing == false) {
            if (fNotificationClient)
                fNotificationClient->prepareForDraw();
            fIsOnCurrentThread = true;
            this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
        }
    }
}

bool SkLightDeferredCanvas::isDeferredDrawing() const {
    return fDeferredDrawing;
}

bool SkLightDeferredCanvas::isFreshFrame() const {
    return this->getDeferredDevice()->isFreshFrame();
}

bool SkLightDeferredCanvas::hasPendingCommands() const {
    // in case of threaded playback, internal mutex is locked
    return this->getDeferredDevice()->hasPendingCommands();
}

void SkLightDeferredCanvas::silentFlush() {
    if (fDeferredDrawing) {
        this->getDeferredDevice()->flushPendingCommands(SkRecordQueue::kSilentPlayback_Mode);
    }
}

SkLightDeferredCanvas::~SkLightDeferredCanvas() {
}

SkSurface* SkLightDeferredCanvas::setSurface(SkSurface* surface) {
    SkLightDeferredDevice* deferredDevice = this->getDeferredDevice();
    SkASSERT(NULL != deferredDevice);
    // By swapping the surface into the existing device, we preserve
    // all pending commands, which can help to seamlessly recover from
    // a lost accelerated graphics context.
    deferredDevice->setSurface(surface);
    return surface;
}

SkLightDeferredCanvas::NotificationClient* SkLightDeferredCanvas::setNotificationClient(
    NotificationClient* notificationClient) {

    SkLightDeferredDevice* deferredDevice = this->getDeferredDevice();
    SkASSERT(deferredDevice);
    if (deferredDevice) {
        deferredDevice->setNotificationClient(notificationClient);
    }

    fNotificationClient = notificationClient;
    return notificationClient;
}

SkImage* SkLightDeferredCanvas::newImageSnapshot() {
    SkLightDeferredDevice* deferredDevice = this->getDeferredDevice();
    SkASSERT(deferredDevice);

    fIsOnCurrentThread = true;
    return deferredDevice ? deferredDevice->newImageSnapshot() : NULL;
}

void SkLightDeferredCanvas::enableThreadedPlayback(bool enable) {
    if (!fNotificationClient || !fDeferredDrawing)
        return;

    this->getDeferredDevice()->enableThreadedPlayback(enable);
    fIsThreadedPlayback = enable;
    SkLightDeferredDevice *deferredDevice = this->getDeferredDevice();
    if (deferredDevice)
        fIsOnCurrentThread = deferredDevice->getOnCurrentThread();
}

bool SkLightDeferredCanvas::isFullFrame(const SkRect* rect,
                                   const SkPaint* paint) const {
    // FIXME: SkRecordQueue does not have matix state
    if (fDeferredDrawing)
        return false;
    SkCanvas* canvas = this->immediateCanvas();
    if (!canvas)
        return false;

    SkISize canvasSize = this->getDeviceSize();
    if (rect) {
        if (!canvas->getTotalMatrix().rectStaysRect()) {
            return false; // conservative
        }

        SkRect transformedRect;
        canvas->getTotalMatrix().mapRect(&transformedRect, *rect);

        if (paint) {
            SkPaint::Style paintStyle = paint->getStyle();
            if (!(paintStyle == SkPaint::kFill_Style ||
                paintStyle == SkPaint::kStrokeAndFill_Style)) {
                return false;
            }
            if (paint->getMaskFilter() || paint->getLooper()
                || paint->getPathEffect() || paint->getImageFilter()) {
                return false; // conservative
            }
        }

        // The following test holds with AA enabled, and is conservative
        // by a 0.5 pixel margin with AA disabled
        if (transformedRect.fLeft > SkIntToScalar(0) ||
            transformedRect.fTop > SkIntToScalar(0) ||
            transformedRect.fRight < SkIntToScalar(canvasSize.fWidth) ||
            transformedRect.fBottom < SkIntToScalar(canvasSize.fHeight)) {
            return false;
        }
    }

    return this->getClipStack()->quickContains(SkRect::MakeXYWH(0, 0,
        SkIntToScalar(canvasSize.fWidth), SkIntToScalar(canvasSize.fHeight)));
}

void SkLightDeferredCanvas::translate(SkScalar dx, SkScalar dy) {
    if (fDeferredDrawing) {
        SkLightDeferredDevice* deferredDevice = this->getDeferredDevice();
        fIsOnCurrentThread = deferredDevice->getOnCurrentThread();
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            deferredDevice->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            deferredDevice->setOnCurrentThread(fIsOnCurrentThread);
        }
        deferredDevice->recorder().translate(dx, dy);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->translate(dx,dy);
    return this->INHERITED::translate(dx, dy);

}

void SkLightDeferredCanvas::scale(SkScalar sx, SkScalar sy) {
    if (fDeferredDrawing) {
        SkLightDeferredDevice* deferredDevice = this->getDeferredDevice();
        fIsOnCurrentThread = deferredDevice->getOnCurrentThread();
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            deferredDevice->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            deferredDevice->setOnCurrentThread(fIsOnCurrentThread);
        }
        deferredDevice->recorder().scale(sx, sy);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->scale(sx,sy);
    return this->INHERITED::scale(sx, sy);

}

void SkLightDeferredCanvas::rotate(SkScalar degrees) {
    if (fDeferredDrawing) {
        SkLightDeferredDevice* deferredDevice = this->getDeferredDevice();
        fIsOnCurrentThread = deferredDevice->getOnCurrentThread();
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            deferredDevice->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            deferredDevice->setOnCurrentThread(fIsOnCurrentThread);
        }
        deferredDevice->recorder().rotate(degrees);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->rotate(degrees);
    return this->INHERITED::rotate(degrees);
}

void SkLightDeferredCanvas::skew(SkScalar sx, SkScalar sy) {
    if (fDeferredDrawing) {
        SkLightDeferredDevice* deferredDevice = this->getDeferredDevice();
        fIsOnCurrentThread = deferredDevice->getOnCurrentThread();
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            deferredDevice->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            deferredDevice->setOnCurrentThread(fIsOnCurrentThread);
        }
        deferredDevice->recorder().skew(sx, sy);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->skew(sx, sy);
    return this->INHERITED::skew(sx, sy);
}

void SkLightDeferredCanvas::willSave() {
    if (fDeferredDrawing) {
        SkLightDeferredDevice* deferredDevice = this->getDeferredDevice();

        fIsOnCurrentThread = deferredDevice->getOnCurrentThread();
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            deferredDevice->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            deferredDevice->setOnCurrentThread(fIsOnCurrentThread);
        }
        SaveFlags flags;
        deferredDevice->recorder().save(flags);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->save();

    this->INHERITED::willSave();
}

SkCanvas::SaveLayerStrategy SkLightDeferredCanvas::willSaveLayer(const SkRect* bounds,
                                                            const SkPaint* paint, SaveFlags flags) {
    if (fDeferredDrawing) {
        SkLightDeferredDevice* deferredDevice = this->getDeferredDevice();
        fIsOnCurrentThread = deferredDevice->getOnCurrentThread();
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            deferredDevice->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            deferredDevice->setOnCurrentThread(fIsOnCurrentThread);
        }
        deferredDevice->recorder().saveLayer(bounds, paint, flags);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->saveLayer(bounds, paint, flags);
    this->INHERITED::willSaveLayer(bounds, paint, flags);
    // No need for a full layer.
    return kFullLayer_SaveLayerStrategy;
}

void SkLightDeferredCanvas::willRestore() {
    if (fDeferredDrawing) {
        SkLightDeferredDevice* deferredDevice = this->getDeferredDevice();
        fIsOnCurrentThread = deferredDevice->getOnCurrentThread();
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            deferredDevice->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            deferredDevice->setOnCurrentThread(fIsOnCurrentThread);
        }
        deferredDevice->recorder().restore();
    }
    else
        this->getDeferredDevice()->immediateCanvas()->restore();
    this->INHERITED::willRestore();
}

bool SkLightDeferredCanvas::isDrawingToLayer() const {
    if (fDeferredDrawing) {
        SkLightDeferredDevice* deferredDevice = this->getDeferredDevice();
        return deferredDevice->recorder().isDrawingToLayer();
    }

    return this->getDeferredDevice()->immediateCanvas()->isDrawingToLayer();
}

void SkLightDeferredCanvas::didConcat(const SkMatrix& matrix) {
    if (fDeferredDrawing) {
        SkLightDeferredDevice* deferredDevice = this->getDeferredDevice();
        fIsOnCurrentThread = deferredDevice->getOnCurrentThread();
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            deferredDevice->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            deferredDevice->setOnCurrentThread(fIsOnCurrentThread);
        }
        deferredDevice->recorder().concat(matrix);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->concat(matrix);

    this->INHERITED::didConcat(matrix);
}

void SkLightDeferredCanvas::didSetMatrix(const SkMatrix& matrix) {
    if (fDeferredDrawing) {
        SkLightDeferredDevice* deferredDevice = this->getDeferredDevice();
        fIsOnCurrentThread = deferredDevice->getOnCurrentThread();
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            deferredDevice->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            deferredDevice->setOnCurrentThread(fIsOnCurrentThread);
        }
        deferredDevice->recorder().setMatrix(matrix);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->setMatrix(matrix);
    this->INHERITED::didSetMatrix(matrix);
}

void SkLightDeferredCanvas::onClipRect(const SkRect& rect,
                                       SkRegion::Op op,
                                       ClipEdgeStyle edgeStyle) {
    if (fDeferredDrawing) {
        SkLightDeferredDevice* deferredDevice = this->getDeferredDevice();
        fIsOnCurrentThread = deferredDevice->getOnCurrentThread();
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            deferredDevice->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            deferredDevice->setOnCurrentThread(fIsOnCurrentThread);
        }
        deferredDevice->recorder().clipRect(rect, op, edgeStyle == kSoft_ClipEdgeStyle);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->clipRect(rect, op, kSoft_ClipEdgeStyle == edgeStyle);
    this->INHERITED::onClipRect(rect, op, edgeStyle);
}

void SkLightDeferredCanvas::onClipRRect(const SkRRect& rrect,
                                   SkRegion::Op op,
                                   ClipEdgeStyle edgeStyle) {
    if (fDeferredDrawing) {
        SkLightDeferredDevice* deferredDevice = this->getDeferredDevice();
        fIsOnCurrentThread = deferredDevice->getOnCurrentThread();
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            deferredDevice->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            deferredDevice->setOnCurrentThread(fIsOnCurrentThread);
        }
        deferredDevice->recorder().clipRRect(rrect, op, edgeStyle == kSoft_ClipEdgeStyle);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->clipRRect(rrect, op, kSoft_ClipEdgeStyle == edgeStyle);
    this->INHERITED::onClipRRect(rrect, op, edgeStyle);
}

void SkLightDeferredCanvas::onClipPath(const SkPath& path,
                                  SkRegion::Op op,
                                  ClipEdgeStyle edgeStyle) {
    if (fDeferredDrawing) {
        SkLightDeferredDevice* deferredDevice = this->getDeferredDevice();
        fIsOnCurrentThread = deferredDevice->getOnCurrentThread();
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            deferredDevice->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            deferredDevice->setOnCurrentThread(fIsOnCurrentThread);
        }
        deferredDevice->recorder().clipPath(path, op, edgeStyle == kSoft_ClipEdgeStyle);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->clipPath(path, op, kSoft_ClipEdgeStyle == edgeStyle);
    this->INHERITED::onClipPath(path, op, edgeStyle);
}

void SkLightDeferredCanvas::onClipRegion(const SkRegion& deviceRgn, SkRegion::Op op) {
    if (fDeferredDrawing) {
        SkLightDeferredDevice* deferredDevice = this->getDeferredDevice();
        fIsOnCurrentThread = deferredDevice->getOnCurrentThread();
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            deferredDevice->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            deferredDevice->setOnCurrentThread(fIsOnCurrentThread);
        }
        deferredDevice->recorder().clipRegion(deviceRgn, op);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->clipRegion(deviceRgn, op);
    this->INHERITED::onClipRegion(deviceRgn, op);
}

void SkLightDeferredCanvas::clear(SkColor color) {
    // purge pending commands
    if (fDeferredDrawing) {
        fIsOnCurrentThread = this->getDeferredDevice()->getOnCurrentThread();
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            this->getDeferredDevice()->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
        }
        this->getDeferredDevice()->skipPendingCommands();
        this->getDeferredDevice()->recorder().clear(color);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->clear(color);
}

void SkLightDeferredCanvas::drawPaint(const SkPaint& paint) {
    if (fDeferredDrawing)
        fIsOnCurrentThread = this->getDeferredDevice()->getOnCurrentThread();
    if (fDeferredDrawing && fIsThreadedPlayback && fIsOnCurrentThread) {
        if (fNotificationClient)
            fNotificationClient->finishDraw();
        this->getDeferredDevice()->recorder().notifyClientForPrepareForDraw();
        fIsOnCurrentThread = false;
        this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
    }

    if (fDeferredDrawing && this->isFullFrame(NULL, &paint) &&
        isPaintOpaque(&paint)) {
        this->getDeferredDevice()->skipPendingCommands();
    }
    LightAutoImmediateDrawIfNeeded autoDraw(*this, &paint);
    if (fDeferredDrawing) {
        this->getDeferredDevice()->recorder().drawPaint(paint);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->drawPaint(paint);
}

void SkLightDeferredCanvas::drawPoints(PointMode mode, size_t count,
                                  const SkPoint pts[], const SkPaint& paint) {
    LightAutoImmediateDrawIfNeeded autoDraw(*this, &paint);
    if (fDeferredDrawing) {
        fIsOnCurrentThread = this->getDeferredDevice()->getOnCurrentThread();
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            this->getDeferredDevice()->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
        }
        this->getDeferredDevice()->recorder().drawPoints(mode, count, pts, paint);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->drawPoints(mode, count, pts, paint);
}

void SkLightDeferredCanvas::drawOval(const SkRect& rect, const SkPaint& paint) {
    LightAutoImmediateDrawIfNeeded autoDraw(*this, &paint);
    if (fDeferredDrawing) {
        fIsOnCurrentThread = this->getDeferredDevice()->getOnCurrentThread();
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            this->getDeferredDevice()->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
        }
        this->getDeferredDevice()->recorder().drawOval(rect, paint);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->drawOval(rect, paint);
}

void SkLightDeferredCanvas::drawRect(const SkRect& rect, const SkPaint& paint) {
    if (fDeferredDrawing)
        fIsOnCurrentThread = this->getDeferredDevice()->getOnCurrentThread();
    if (fDeferredDrawing && fIsThreadedPlayback && fIsOnCurrentThread) {
        if (fNotificationClient)
            fNotificationClient->finishDraw();
        this->getDeferredDevice()->recorder().notifyClientForPrepareForDraw();
        fIsOnCurrentThread = false;
        this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
    }

    if (fDeferredDrawing && this->isFullFrame(&rect, &paint) &&
        isPaintOpaque(&paint)) {
        this->getDeferredDevice()->skipPendingCommands();
    }

    LightAutoImmediateDrawIfNeeded autoDraw(*this, &paint);
    if (fDeferredDrawing) {
        this->getDeferredDevice()->recorder().drawRect(rect, paint);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->drawRect(rect, paint);
}

void SkLightDeferredCanvas::drawRRect(const SkRRect& rrect, const SkPaint& paint) {
    if (fDeferredDrawing)
        fIsOnCurrentThread = this->getDeferredDevice()->getOnCurrentThread();
    if (fDeferredDrawing && fIsThreadedPlayback && fIsOnCurrentThread) {
        if (fNotificationClient)
            fNotificationClient->finishDraw();
        this->getDeferredDevice()->recorder().notifyClientForPrepareForDraw();
        fIsOnCurrentThread = false;
        this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
    }

    if (rrect.isRect()) {
        this->SkLightDeferredCanvas::drawRect(rrect.getBounds(), paint);
    } else if (rrect.isOval()) {
        this->SkLightDeferredCanvas::drawOval(rrect.getBounds(), paint);
    } else {
        LightAutoImmediateDrawIfNeeded autoDraw(*this, &paint);
        if (fDeferredDrawing) {
            this->getDeferredDevice()->recorder().drawRRect(rrect, paint);
        }
        else
            this->getDeferredDevice()->immediateCanvas()->drawRRect(rrect, paint);
    }
}

void SkLightDeferredCanvas::onDrawDRRect(const SkRRect& outer, const SkRRect& inner,
                                    const SkPaint& paint) {
    LightAutoImmediateDrawIfNeeded autoDraw(*this, &paint);
    if (fDeferredDrawing) {
        fIsOnCurrentThread = this->getDeferredDevice()->getOnCurrentThread();
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            this->getDeferredDevice()->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
        }
        this->getDeferredDevice()->recorder().drawDRRect(outer, inner, paint);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->drawDRRect(outer, inner, paint);
}

void SkLightDeferredCanvas::drawPath(const SkPath& path, const SkPaint& paint) {
    LightAutoImmediateDrawIfNeeded autoDraw(*this, &paint);
    if (fDeferredDrawing) {
        fIsOnCurrentThread = this->getDeferredDevice()->getOnCurrentThread();
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            this->getDeferredDevice()->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
        }
        this->getDeferredDevice()->recorder().drawPath(path, paint);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->drawPath(path, paint);
}

void SkLightDeferredCanvas::drawBitmap(const SkBitmap& bitmap, SkScalar left,
                                  SkScalar top, const SkPaint* paint) {
    if (fDeferredDrawing)
        fIsOnCurrentThread = this->getDeferredDevice()->getOnCurrentThread();
    if (fDeferredDrawing && fIsThreadedPlayback && fIsOnCurrentThread) {
        if (fNotificationClient)
            fNotificationClient->finishDraw();
        this->getDeferredDevice()->recorder().notifyClientForPrepareForDraw();
        fIsOnCurrentThread = false;
        this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
    }

    SkRect bitmapRect = SkRect::MakeXYWH(left, top,
        SkIntToScalar(bitmap.width()), SkIntToScalar(bitmap.height()));
    if (fDeferredDrawing &&
        this->isFullFrame(&bitmapRect, paint) &&
        isPaintOpaque(paint, &bitmap)) {
        this->getDeferredDevice()->skipPendingCommands();
    }

    LightAutoImmediateDrawIfNeeded autoDraw(*this, &bitmap, paint);
    if (fDeferredDrawing) {
        this->getDeferredDevice()->recorder().drawBitmap(bitmap, left, top, paint);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->drawBitmap(bitmap, left, top, paint);
}

void SkLightDeferredCanvas::drawBitmapRectToRect(const SkBitmap& bitmap,
                                            const SkRect* src,
                                            const SkRect& dst,
                                            const SkPaint* paint,
                                            DrawBitmapRectFlags flags) {
    if (fDeferredDrawing)
        fIsOnCurrentThread = this->getDeferredDevice()->getOnCurrentThread();
    if (fDeferredDrawing && fIsThreadedPlayback && fIsOnCurrentThread) {
        if (fNotificationClient)
            fNotificationClient->finishDraw();
        this->getDeferredDevice()->recorder().notifyClientForPrepareForDraw();
        fIsOnCurrentThread = false;
        this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
    }

    if (fDeferredDrawing &&
        this->isFullFrame(&dst, paint) &&
        isPaintOpaque(paint, &bitmap)) {
        this->getDeferredDevice()->skipPendingCommands();
    }

    LightAutoImmediateDrawIfNeeded autoDraw(*this, &bitmap, paint);
    if (fDeferredDrawing)
        this->getDeferredDevice()->recorder().drawBitmapRectToRect(bitmap, src, dst, paint, flags);
    else
        this->getDeferredDevice()->immediateCanvas()->drawBitmapRectToRect(bitmap, src, dst, paint, flags);
}


void SkLightDeferredCanvas::drawBitmapMatrix(const SkBitmap& bitmap,
                                        const SkMatrix& m,
                                        const SkPaint* paint) {
    // TODO: reset recording canvas if paint+bitmap is opaque and clip rect
    // covers canvas entirely and transformed bitmap covers canvas entirely
    if (fDeferredDrawing)
        fIsOnCurrentThread = this->getDeferredDevice()->getOnCurrentThread();
    LightAutoImmediateDrawIfNeeded autoDraw(*this, &bitmap, paint);
    if (fDeferredDrawing) {
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            this->getDeferredDevice()->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
        }
        this->getDeferredDevice()->recorder().drawBitmapMatrix(bitmap, m, paint);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->drawBitmapMatrix(bitmap, m, paint);
}

void SkLightDeferredCanvas::drawBitmapNine(const SkBitmap& bitmap,
                                      const SkIRect& center, const SkRect& dst,
                                      const SkPaint* paint) {
    // TODO: reset recording canvas if paint+bitmap is opaque and clip rect
    // covers canvas entirely and dst covers canvas entirely
    if (fDeferredDrawing)
        fIsOnCurrentThread = this->getDeferredDevice()->getOnCurrentThread();
    LightAutoImmediateDrawIfNeeded autoDraw(*this, &bitmap, paint);
    if (fDeferredDrawing) {
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            this->getDeferredDevice()->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
        }
        this->getDeferredDevice()->recorder().drawBitmapNine(bitmap, center, dst, paint);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->drawBitmapNine(bitmap, center, dst, paint);
}

void SkLightDeferredCanvas::drawSprite(const SkBitmap& bitmap, int left, int top,
                                  const SkPaint* paint) {
    if (fDeferredDrawing)
        fIsOnCurrentThread = this->getDeferredDevice()->getOnCurrentThread();
    if (fDeferredDrawing && fIsThreadedPlayback && fIsOnCurrentThread) {
        if (fNotificationClient)
            fNotificationClient->finishDraw();
        this->getDeferredDevice()->recorder().notifyClientForPrepareForDraw();
        fIsOnCurrentThread = false;
        this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
    }

    SkRect bitmapRect = SkRect::MakeXYWH(
        SkIntToScalar(left),
        SkIntToScalar(top),
        SkIntToScalar(bitmap.width()),
        SkIntToScalar(bitmap.height()));
    if (fDeferredDrawing &&
        this->isFullFrame(&bitmapRect, paint) &&
        isPaintOpaque(paint, &bitmap)) {
        this->getDeferredDevice()->skipPendingCommands();
    }

    LightAutoImmediateDrawIfNeeded autoDraw(*this, &bitmap, paint);
    if (fDeferredDrawing)
        this->getDeferredDevice()->recorder().drawSprite(bitmap, left, top, paint);
    else
        this->getDeferredDevice()->immediateCanvas()->drawSprite(bitmap, left, top, paint);
}

void SkLightDeferredCanvas::onDrawText(const void* text, size_t byteLength, SkScalar x, SkScalar y,
                                  const SkPaint& paint) {
    LightAutoImmediateDrawIfNeeded autoDraw(*this, &paint);
    if (fDeferredDrawing)
        fIsOnCurrentThread = this->getDeferredDevice()->getOnCurrentThread();
    if (fDeferredDrawing) {
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            this->getDeferredDevice()->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
        }
        this->getDeferredDevice()->recorder().drawText(text, byteLength, x, y, paint);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->drawText(text, byteLength, x, y, paint);
}

void SkLightDeferredCanvas::onDrawPosText(const void* text, size_t byteLength, const SkPoint pos[],
                                     const SkPaint& paint) {
    if (fDeferredDrawing)
        fIsOnCurrentThread = this->getDeferredDevice()->getOnCurrentThread();
    LightAutoImmediateDrawIfNeeded autoDraw(*this, &paint);
    if (fDeferredDrawing) {
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
           this->getDeferredDevice()->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
        }
        this->getDeferredDevice()->recorder().drawPosText(text, byteLength, pos, paint);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->drawPosText(text, byteLength, pos, paint);
}

void SkLightDeferredCanvas::onDrawPosTextH(const void* text, size_t byteLength, const SkScalar xpos[],
                                      SkScalar constY, const SkPaint& paint) {
    if (fDeferredDrawing)
        fIsOnCurrentThread = this->getDeferredDevice()->getOnCurrentThread();
    LightAutoImmediateDrawIfNeeded autoDraw(*this, &paint);
    if (fDeferredDrawing) {
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            this->getDeferredDevice()->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
        }
        this->getDeferredDevice()->recorder().drawPosTextH(text, byteLength, xpos, constY, paint);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->drawPosTextH(text, byteLength, xpos, constY, paint);
}

void SkLightDeferredCanvas::onDrawTextOnPath(const void* text, size_t byteLength, const SkPath& path,
                                        const SkMatrix* matrix, const SkPaint& paint) {
    if (fDeferredDrawing)
        fIsOnCurrentThread = this->getDeferredDevice()->getOnCurrentThread();
    LightAutoImmediateDrawIfNeeded autoDraw(*this, &paint);
    if (fDeferredDrawing) {
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            this->getDeferredDevice()->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
        }
        this->getDeferredDevice()->recorder().drawTextOnPath(text, byteLength, path, matrix, paint);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->drawTextOnPath(text, byteLength, path, matrix, paint);
}

void SkLightDeferredCanvas::onDrawPicture(const SkPicture* picture) {
    if (fDeferredDrawing)
        fIsOnCurrentThread = this->getDeferredDevice()->getOnCurrentThread();
    if (fDeferredDrawing) {
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            this->getDeferredDevice()->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
        }
        this->getDeferredDevice()->recorder().drawPicture(picture);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->drawPicture(picture);
}

void SkLightDeferredCanvas::drawVertices(VertexMode vmode, int vertexCount,
                                    const SkPoint vertices[],
                                    const SkPoint texs[],
                                    const SkColor colors[], SkXfermode* xmode,
                                    const uint16_t indices[], int indexCount,
                                    const SkPaint& paint) {
    if (fDeferredDrawing)
        fIsOnCurrentThread = this->getDeferredDevice()->getOnCurrentThread();
    if (fDeferredDrawing && fIsThreadedPlayback && fIsOnCurrentThread) {
        if (fNotificationClient)
            fNotificationClient->finishDraw();
        this->getDeferredDevice()->recorder().notifyClientForPrepareForDraw();
        fIsOnCurrentThread = false;
        this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
    }

    LightAutoImmediateDrawIfNeeded autoDraw(*this, &paint);
    if (fDeferredDrawing)
        this->getDeferredDevice()->recorder().drawVertices(vmode, vertexCount,
                                                            vertices, texs,
                                                            colors, xmode,
                                                            indices, indexCount, paint);
    else
        this->getDeferredDevice()->immediateCanvas()->drawVertices(vmode, vertexCount, vertices, texs, colors, xmode,
                                        indices, indexCount, paint);
}

SkDrawFilter* SkLightDeferredCanvas::setDrawFilter(SkDrawFilter* filter) {
    if (fDeferredDrawing) {
        fIsOnCurrentThread = this->getDeferredDevice()->getOnCurrentThread();
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            this->getDeferredDevice()->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
        }
        this->getDeferredDevice()->recorder().setDrawFilter(filter);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->setDrawFilter(filter);
    this->INHERITED::setDrawFilter(filter);
    return filter;
}

void SkLightDeferredCanvas::onPushCull(const SkRect& rect)
{
    if (fDeferredDrawing) {
        fIsOnCurrentThread = this->getDeferredDevice()->getOnCurrentThread();
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            this->getDeferredDevice()->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
        }
        this->getDeferredDevice()->recorder().pushCull(rect);
    }
    else
        this->getDeferredDevice()->immediateCanvas()->pushCull(rect);
    this->INHERITED::onPushCull(rect);
}

void SkLightDeferredCanvas::onPopCull()
{
    if (fDeferredDrawing) {
        fIsOnCurrentThread = this->getDeferredDevice()->getOnCurrentThread();
        if (fIsThreadedPlayback && fIsOnCurrentThread) {
            if (fNotificationClient)
                fNotificationClient->finishDraw();
            this->getDeferredDevice()->recorder().notifyClientForPrepareForDraw();
            fIsOnCurrentThread = false;
            this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
        }
        this->getDeferredDevice()->recorder().popCull();
    }
    else
        this->getDeferredDevice()->immediateCanvas()->popCull();
    this->INHERITED::onPopCull();
}

SkCanvas* SkLightDeferredCanvas::canvasForDrawIter() {
    // FIXME: do we have to have SkRecordQueue as subclass of SkCanvas?
    if (fDeferredDrawing)
        return NULL;
    return this->getDeferredDevice()->immediateCanvas();
}

void SkLightDeferredCanvas::flushPendingCommands() {
    if (fDeferredDrawing)
        fIsOnCurrentThread = this->getDeferredDevice()->getOnCurrentThread();
    if (fDeferredDrawing && fIsThreadedPlayback && fIsOnCurrentThread) {
        if (fNotificationClient)
            fNotificationClient->finishDraw();
        this->getDeferredDevice()->recorder().notifyClientForPrepareForDraw();
        fIsOnCurrentThread = false;
        this->getDeferredDevice()->setOnCurrentThread(fIsOnCurrentThread);
    }
    this->getDeferredDevice()->flushPendingCommands(SkRecordQueue::kSilentPlayback_Mode);
}

void SkLightDeferredCanvas::flush()
{
    if (fDeferredDrawing) {
        this->getDeferredDevice()->flushPendingCommands(SkRecordQueue::kNormalPlayback_Mode);
        this->getDeferredDevice()->recorder().flush();
    } else
        this->getDeferredDevice()->immediateCanvas()->flush();
    this->INHERITED::flush();
}
