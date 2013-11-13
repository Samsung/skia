#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "include/gpu/gl/GrGLInterface.h"
#include "src/gpu/gl/GrGLUtil.h"
#include "include/gpu/GrRenderTarget.h"
#include "include/gpu/GrContext.h"
#include "include/gpu/SkGpuDevice.h"

static Display* getDisplay()
{
    static Display* display = NULL;
    if (!display)
        display = XOpenDisplay(NULL);
    return display;
}

static EGLDisplay getEGLDisplay()
{
    static EGLDisplay display = EGL_NO_DISPLAY;
    if (display == EGL_NO_DISPLAY)
        display = eglGetDisplay((EGLNativeDisplayType) getDisplay());
    return display;
}

static Window createWindow(int width, int height)
{
    Window window = XCreateSimpleWindow(getDisplay(), DefaultRootWindow(getDisplay()), 0, 0, width, height, 0, 0, 0);
    XMapWindow(getDisplay(), window);
    return window;
}

static bool createEGLContextWithWindow(Window window, EGLContext* context, EGLSurface* surface)
{
    EGLint eglAttributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 1,
        EGL_GREEN_SIZE, 1,
        EGL_BLUE_SIZE, 1,
        EGL_ALPHA_SIZE, 1,
        EGL_STENCIL_SIZE, 1,
        EGL_SAMPLES, 4,
        EGL_SAMPLE_BUFFERS, 1,
        EGL_NONE,
    };

    EGLint contextAttributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    EGLConfig eglConfig;
    EGLint number;
    if (!eglChooseConfig(getEGLDisplay(), eglAttributes, &eglConfig, 1, &number) || number == 0) {
        printf("Cannot choose EGL config\n");
        return false;
    }

    *context = eglCreateContext(getEGLDisplay(), eglConfig, NULL, contextAttributes);
    if (*context == EGL_NO_CONTEXT) {
        printf("Cannot create EGL context\n");
        return false;
    }

    *surface = eglCreateWindowSurface(getEGLDisplay(), eglConfig, (NativeWindowType) window, NULL);
    if (*surface == EGL_NO_SURFACE) {
        printf("Cannot create egl window surface\n");
        return false;
    }

    return true;
}

static bool initializeEGL()
{
    EGLint major, minor;
    if (!eglInitialize(getEGLDisplay(), &major, &minor)) {
        printf("Cannot initialize EGL\n");
        return false;
    }

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        printf ("Cannot bind EGL to GLES2 API\n");
        return false;
    }

    return true;
}

static void draw()
{
    GrBackendRenderTargetDesc desc;
    desc.fWidth = 720;
    desc.fHeight = 1280;
    desc.fConfig = kSkia8888_GrPixelConfig;
    desc.fOrigin = kBottomLeft_GrSurfaceOrigin;
    desc.fSampleCnt = 4;
    desc.fStencilBits = 1;
    desc.fRenderTargetHandle = 0;
    GrContext* ctx = GrContext::Create(kOpenGL_GrBackend, 0);
    GrRenderTarget* target = ctx->wrapBackendRenderTarget(desc);

    SkCanvas* canvas = new SkCanvas(new SkGpuDevice(ctx, target));
    SkPaint paint;
    paint.setColor(0xFF66AAEE);

    SkRect r;
    r.set(0, 0, 200, 200);
    canvas->drawRect(r, paint);

    ctx->flush();
}

int main (int argc, char **argv)
{
    Window window = createWindow(720, 1280);

    if (!initializeEGL())
        goto FINISH_X;

    EGLContext context;
    EGLSurface surface;
    if (!createEGLContextWithWindow(window, &context, &surface))
        goto FINISH_X;

    eglMakeCurrent(getEGLDisplay(), surface, surface, context);
    glClearColor(1, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    draw();

    eglSwapBuffers(getEGLDisplay(), surface);

    sleep(5);

    eglDestroyContext(getEGLDisplay(), context);
    eglDestroySurface(getEGLDisplay(), surface);

FINISH_X:
    XUnmapWindow(getDisplay(), window);
    XDestroyWindow(getDisplay(), window);
    XCloseDisplay(getDisplay());

    return 0;
}
