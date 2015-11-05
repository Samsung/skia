
/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <wayland-client.h>
#include <wayland-egl.h>

//#include <X11/Xlib.h>
//#include <X11/Xatom.h>
#include <X11/XKBlib.h>
//#include <GL/glx.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

//#include <GL/gl.h>
//#include <GL/glu.h>
#include <stdio.h>

#include "SkWindow.h"

#include "SkBitmap.h"
#include "SkCanvas.h"
#include "SkColor.h"
#include "SkEvent.h"
#include "SkKey.h"
#include "SkWindow.h"
#include "XkeysToSkKeys.h"
extern "C" {
    #include "keysym2ucs.h"
}

const int WIDTH = 500;
const int HEIGHT = 500;
struct wayland_data {
     struct wl_display *display;
     struct wl_compositor *compositor;
     struct wl_shell *shell;
};

struct wayland_window {
     struct wl_surface *surface;
     struct wl_shell_surface *shell_surface;
     struct wl_egl_window *egl_window;
};

static struct wayland_window window;
static struct wayland_data wayland;
struct wl_keyboard *keyboard;
struct wl_seat *seat;
struct wl_pointer *pointer;
SkWindow *This;

static void
keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
                       uint32_t format, int fd, uint32_t size)
{
}

static void
keyboard_handle_enter(void *data, struct wl_keyboard *keyboard,
                      uint32_t serial, struct wl_surface *surface,
                      struct wl_array *keys)
{
    fprintf(stderr, "Keyboard gained focus\n");
}

static void
keyboard_handle_leave(void *data, struct wl_keyboard *keyboard,
                      uint32_t serial, struct wl_surface *surface)
{
    fprintf(stderr, "Keyboard lost focus\n");
}

static void
keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
                    uint32_t serial, uint32_t time, uint32_t key,
                    uint32_t state)
{
    key+=8; // Conversion from X to Wayland keyhandling
    static Display *display = NULL;
    if(display == NULL)
        display = XOpenDisplay(NULL);

    if (state == 1) {
        state = 16;
        int shiftLevel = (state & ShiftMask) ? 1 : 0;
        KeySym keysym = XkbKeycodeToKeysym(display, key,
                                           0, shiftLevel);
        long uni = keysym2ucs(keysym);
        if (uni != -1) {
            This->handleChar((SkUnichar) uni);
        }
    }
    if (state == 0) {
        This->handleKeyUp(XKeyToSkKey(key));
    }
}

static void
keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard,
                          uint32_t serial, uint32_t mods_depressed,
                          uint32_t mods_latched, uint32_t mods_locked,
                          uint32_t group)
{
    fprintf(stderr, "Modifiers depressed %d, latched %d, locked %d, group %d\n",
            mods_depressed, mods_latched, mods_locked, group);
}

static const struct wl_keyboard_listener keyboard_listener = {
    keyboard_handle_keymap,
    keyboard_handle_enter,
    keyboard_handle_leave,
    keyboard_handle_key,
    keyboard_handle_modifiers,
};



struct pointer_data {
    struct wl_surface *surface;
    struct wl_buffer *buffer;
    int32_t hot_spot_x;
    int32_t hot_spot_y;
    struct wl_surface *target_surface;
};


static void pointer_enter(void *data,
    struct wl_pointer *wl_pointer,
    uint32_t serial, struct wl_surface *surface,
    wl_fixed_t surface_x, wl_fixed_t surface_y)
{
}

static void pointer_leave(void *data,
    struct wl_pointer *wl_pointer, uint32_t serial,
    struct wl_surface *wl_surface) { }

static void pointer_motion(void *data,
    struct wl_pointer *wl_pointer, uint32_t time,
    wl_fixed_t surface_x, wl_fixed_t surface_y) { }

static void pointer_button(void *data,
    struct wl_pointer *wl_pointer, uint32_t serial,
    uint32_t time, uint32_t button, uint32_t state)
{
}

static void pointer_axis(void *data,
    struct wl_pointer *wl_pointer, uint32_t time,
    uint32_t axis, wl_fixed_t value) { }


static const struct wl_pointer_listener pointer_listener = {
    pointer_enter,
    pointer_leave,
    pointer_motion,
    pointer_button,
    pointer_axis
};

static void
seat_handle_capabilities(void *data, struct wl_seat *seat,
                         //enum wl_seat_capability caps)
                         uint32_t caps)
{
    if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
        keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
    } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
        wl_keyboard_destroy(keyboard);
        keyboard = NULL;
    }
    if (caps & WL_SEAT_CAPABILITY_POINTER) {
        printf("Display has a pointer\n");
        pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(pointer, &pointer_listener,
            NULL);
    }
}

static const struct wl_seat_listener seat_listener = {
    seat_handle_capabilities,
};

// listeners
static void registry_add_object (void *data, struct wl_registry *registry,
                                 uint32_t name, const char *interface,
                                 uint32_t version) {
    if (!strcmp(interface,"wl_compositor")) {
        wayland.compositor = (wl_compositor *) wl_registry_bind (registry, name, &wl_compositor_interface, 0);
    } else if (!strcmp(interface,"wl_shell")) {
        wayland.shell = (wl_shell *) wl_registry_bind (registry, name, &wl_shell_interface, 0);
    } else if (strcmp(interface, "wl_seat") == 0) {
        seat = (wl_seat*) wl_registry_bind(registry, name,
                &wl_seat_interface, 0);
        wl_seat_add_listener(seat, &seat_listener, NULL);
    }
}

static void registry_remove_object (void *data, struct wl_registry *registry, uint32_t name) { }

// wayland registry listener
static struct wl_registry_listener registry_listener = {
    &registry_add_object,
    &registry_remove_object
};

static void shell_surface_ping (void *data, struct wl_shell_surface *shell_surface, uint32_t serial) {
    wl_shell_surface_pong (shell_surface, serial);
}

static void shell_surface_configure (void *data,
                                     struct wl_shell_surface *shell_surface,
                                     uint32_t edges,
                                     int32_t width, int32_t height) {
    struct wayland_window *window = (struct wayland_window *) data;
    wl_egl_window_resize (window->egl_window, width, height, 0, 0);
}
static void shell_surface_popup_done (void *data, struct wl_shell_surface *shell_surface) { }
// wayland shell surface listener
static struct wl_shell_surface_listener shell_surface_listener = {
    &shell_surface_ping,
    &shell_surface_configure,
    &shell_surface_popup_done
};

static void init_wayland (struct wayland_data *wayland) {
   struct wl_registry *registry;
   wayland->display = wl_display_connect (NULL);
   registry = wl_display_get_registry (wayland->display);
   wl_registry_add_listener (registry, &registry_listener, NULL);

   wl_display_dispatch (wayland->display);
}

static void create_window (struct wayland_data *wayland,
                           struct wayland_window *window,
                           int32_t width, int32_t height) {
    // wayland surface
    window->surface = wl_compositor_create_surface (wayland->compositor);
    // wayland shell surface
    window->shell_surface = wl_shell_get_shell_surface (wayland->shell,
                                                        window->surface);
    // shell surface listener
    wl_shell_surface_add_listener (window->shell_surface,
                                   &shell_surface_listener, window);
    wl_shell_surface_set_toplevel (window->shell_surface);
    // wayland window
    window->egl_window = wl_egl_window_create (window->surface,
                                               width, height);
}

static void delete_window (struct wayland_window *window) {
    wl_egl_window_destroy (window->egl_window);
    wl_shell_surface_destroy (window->shell_surface);
    wl_surface_destroy (window->surface);
}

// Determine which events to listen for.
const long EVENT_MASK = StructureNotifyMask|ButtonPressMask|ButtonReleaseMask
        |ExposureMask|PointerMotionMask|KeyPressMask|KeyReleaseMask;

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


SkOSWindow::SkOSWindow(void*)
    //: fVi(NULL)
    : fMSAASampleCount(0) {
    fUnixWindow.fDisplay = NULL;
    fUnixWindow.fGLContext = NULL;
    this->initWindow(0, NULL);
    this->resize(WIDTH, HEIGHT);
}

SkOSWindow::~SkOSWindow() {
    this->closeWindow();
}

void SkOSWindow::closeWindow() {
    if (fUnixWindow.fDisplay) {
        this->detach();
        SkASSERT(fUnixWindow.fGc);
        XFreeGC(fUnixWindow.fDisplay, fUnixWindow.fGc);
        fUnixWindow.fGc = NULL;
        XDestroyWindow(fUnixWindow.fDisplay, fUnixWindow.fWin);
        //fVi = NULL;
        XCloseDisplay(fUnixWindow.fDisplay);
        fUnixWindow.fDisplay = NULL;
        fMSAASampleCount = 0;
    }
}

void SkOSWindow::initWindow(int requestedMSAASampleCount, AttachmentInfo* info) {
    This = this;
    EGLConfig configs;
    EGLint numConfigs;
    EGLint num;
    EGLDisplay display;
    if (fMSAASampleCount != requestedMSAASampleCount) {
        this->closeWindow();
    }
    // presence of fDisplay means we already have a window
    if (wayland.display) {
        if (info) {
            if (1/*fVi*/) {
                eglGetConfigs(eglGetDisplay (wayland.display), &configs, 1, &numConfigs);
                eglGetConfigAttrib(eglGetDisplay (wayland.display), configs, EGL_SAMPLES, &info->fSampleCount);
                eglGetConfigAttrib(eglGetDisplay (wayland.display), configs, EGL_STENCIL_SIZE, &info->fStencilBits);
                //glXGetConfig(fUnixWindow.fDisplay, fVi, GLX_SAMPLES_ARB, &info->fSampleCount);
                //glXGetConfig(fUnixWindow.fDisplay, fVi, GLX_STENCIL_SIZE, &info->fStencilBits);
            } else {
                info->fSampleCount = 0;
                info->fStencilBits = 0;
            }
        }
        return;
    }

    init_wayland (&wayland);
    //fUnixWindow.fDisplay = XOpenDisplay(NULL);
    Display* dsp = fUnixWindow.fDisplay;
    if (NULL == dsp) {
        SkDebugf("Could not open an X Display \n");
		// Dont return as we dont use Display
    }

    // Attempt to create a window that supports GL
    /*GLint att[] = {
        GLX_RGBA,
        GLX_DEPTH_SIZE, 24,
        GLX_DOUBLEBUFFER,
        GLX_STENCIL_SIZE, 8,
        None
    };*/
    EGLint att[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 1,
        EGL_GREEN_SIZE, 1,
        EGL_BLUE_SIZE, 1,
        EGL_ALPHA_SIZE, 1,
        EGL_STENCIL_SIZE, 1,
        /*EGL_SAMPLES, 0,
        EGL_SAMPLE_BUFFERS,0,*/
        EGL_NONE
    };

    EGLint major, minor;
	create_window (&wayland, &window, WIDTH, HEIGHT);
    display = eglGetDisplay (wayland.display);
    if (display == EGL_NO_DISPLAY) {
        printf ("Cannot get egl display\n");
        exit (-1);
    }

    if (! eglInitialize (display, NULL,NULL)) {
        printf ("Cannot initialize egl\n");
        exit (-1);
    }

    if (! eglBindAPI (EGL_OPENGL_API)) {
        printf ("Cannot bind egl to gles2 API\n");
        exit (-1);
    }

    //SkASSERT(NULL == fVi);
    if (requestedMSAASampleCount > 0) {
        static const GLint kAttCount = SK_ARRAY_COUNT(att);
        GLint msaaAtt[kAttCount + 4];
        memcpy(msaaAtt, att, sizeof(att));
        SkASSERT(None == msaaAtt[kAttCount - 1]);
        //msaaAtt[kAttCount - 1] = GLX_SAMPLE_BUFFERS_ARB;
        msaaAtt[kAttCount - 1] = EGL_SAMPLE_BUFFERS;
        msaaAtt[kAttCount + 0] = 1;
        msaaAtt[kAttCount + 1] = EGL_SAMPLES;
        msaaAtt[kAttCount + 2] = requestedMSAASampleCount;
        msaaAtt[kAttCount + 3] = None;
        if (! eglChooseConfig (display, msaaAtt, &fUnixWindow.eglConfig, 1, &num)) {
            printf ("cannot get egl configuration\n");
            exit (-1);
        }

        //fVi = glXChooseVisual(dsp, DefaultScreen(dsp), msaaAtt);
        fMSAASampleCount = requestedMSAASampleCount;
    } else if (requestedMSAASampleCount == 0) /*if (NULL == fVi)*/ {
        if (! eglChooseConfig (display, att, &fUnixWindow.eglConfig, 1, &num)) {
            printf ("cannot get egl configuration\n");
            exit (-1);
        }
        //fVi = glXChooseVisual(dsp, DefaultScreen(dsp), att);
        fMSAASampleCount = 0;
    }
}

static unsigned getModi(const XEvent& evt) {
    static const struct {
        unsigned    fXMask;
        unsigned    fSkMask;
    } gModi[] = {
        // X values found by experiment. Is there a better way?
        { 1,    kShift_SkModifierKey },
        { 4,    kControl_SkModifierKey },
        { 8,    kOption_SkModifierKey },
    };

    unsigned modi = 0;
    for (size_t i = 0; i < SK_ARRAY_COUNT(gModi); ++i) {
        if (evt.xkey.state & gModi[i].fXMask) {
            modi |= gModi[i].fSkMask;
        }
    }
    return modi;
}

static SkMSec gTimerDelay;

static bool MyXNextEventWithDelay(Display* dsp, XEvent* evt) {
    // Check for pending events before entering the select loop. There might
    // be events in the in-memory queue but not processed yet.
    if (XPending(dsp)) {
        XNextEvent(dsp, evt);
        return true;
    }

    SkMSec ms = gTimerDelay;
    if (ms > 0) {
        int x11_fd = ConnectionNumber(dsp);
        fd_set input_fds;
        FD_ZERO(&input_fds);
        FD_SET(x11_fd, &input_fds);

        timeval tv;
        tv.tv_sec = ms / 1000;              // seconds
        tv.tv_usec = (ms % 1000) * 1000;    // microseconds

        if (!select(x11_fd + 1, &input_fds, NULL, NULL, &tv)) {
            if (!XPending(dsp)) {
                return false;
            }
        }
    }
    XNextEvent(dsp, evt);
    return true;
}

static Atom wm_delete_window_message;

SkOSWindow::NextXEventResult SkOSWindow::nextXEvent() {
    XEvent evt;
    Display* dsp = fUnixWindow.fDisplay;

    if (!MyXNextEventWithDelay(dsp, &evt)) {
        return kContinue_NextXEventResult;
    }
#if 0
    switch (evt.type) {
        case Expose:
            if (0 == evt.xexpose.count) {
                return kPaintRequest_NextXEventResult;
            }
            break;
        case ConfigureNotify:
            this->resize(evt.xconfigure.width, evt.xconfigure.height);
            break;
        case ButtonPress:
            if (evt.xbutton.button == Button1)
                this->handleClick(evt.xbutton.x, evt.xbutton.y,
                            SkView::Click::kDown_State, NULL, getModi(evt));
            break;
        case ButtonRelease:
            if (evt.xbutton.button == Button1)
                this->handleClick(evt.xbutton.x, evt.xbutton.y,
                              SkView::Click::kUp_State, NULL, getModi(evt));
            break;
        case MotionNotify:
            this->handleClick(evt.xmotion.x, evt.xmotion.y,
                           SkView::Click::kMoved_State, NULL, getModi(evt));
            break;
        case KeyPress: {
            int shiftLevel = (evt.xkey.state & ShiftMask) ? 1 : 0;
            KeySym keysym = XkbKeycodeToKeysym(dsp, evt.xkey.keycode,
                                               0, shiftLevel);
            if (keysym == XK_Escape) {
                return kQuitRequest_NextXEventResult;
            }
            this->handleKey(XKeyToSkKey(keysym));
            long uni = keysym2ucs(keysym);
            if (uni != -1) {
                this->handleChar((SkUnichar) uni);
            }
            break;
        }
        case KeyRelease:
            this->handleKeyUp(XKeyToSkKey(XkbKeycodeToKeysym(dsp, evt.xkey.keycode, 0, 0)));
            break;
        case ClientMessage:
            if ((Atom)evt.xclient.data.l[0] == wm_delete_window_message) {
                return kQuitRequest_NextXEventResult;
            }
            // fallthrough
        default:
            // Do nothing for other events
            break;
    }
#endif
    return kContinue_NextXEventResult;
}

void SkOSWindow::loop() {
    Display* dsp = fUnixWindow.fDisplay;
    if (NULL == dsp) {
        // Dont return as we dont use Display
    }

    bool sentExposeEvent = false;

    while (wl_display_dispatch(wayland.display) != -1) {
        SkEvent::ServiceQueueTimer();

        bool moreToDo = SkEvent::ProcessEvent();
        if (this->isDirty()) {
            this->update(NULL);
        }
        this->doPaint();
    }
}

void SkOSWindow::mapWindowAndWait() {
    SkASSERT(fUnixWindow.fDisplay);
    Display* dsp = fUnixWindow.fDisplay;
    Window win = fUnixWindow.fWin;
    XMapWindow(dsp, win);

    long eventMask = StructureNotifyMask;
    XSelectInput(dsp, win, eventMask);

    // Wait until screen is ready.
    XEvent evt;
    do {
        XNextEvent(dsp, &evt);
    } while(evt.type != MapNotify);

}

bool SkOSWindow::attach(SkBackEndTypes, int msaaSampleCount, AttachmentInfo* info) {

    this->initWindow(msaaSampleCount, info);

    if (NULL == fUnixWindow.fDisplay) {
        //return false;
    }
    if (NULL == fUnixWindow.fGLContext) {
        //SkASSERT(fVi);

        EGLint contextAttributes[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE, EGL_NONE };

        fUnixWindow.fGLContext = eglCreateContext(eglGetDisplay (wayland.display), fUnixWindow.eglConfig, EGL_NO_CONTEXT, NULL);


        /*fUnixWindow.fGLContext = glXCreateContext(fUnixWindow.fDisplay,
                                                    fVi,
                                                    NULL,
                                                    GL_TRUE);*/
        if (NULL == fUnixWindow.fGLContext) {
            return false;
        }
        fUnixWindow.fGLSurface = eglCreateWindowSurface(eglGetDisplay (wayland.display), fUnixWindow.eglConfig, window.egl_window, NULL);
        if (fUnixWindow.fGLSurface == EGL_NO_SURFACE) {
            printf("Cannot create egl window surface\n");
            return false;
        }
    }

    eglMakeCurrent(eglGetDisplay (wayland.display), fUnixWindow.fGLSurface, fUnixWindow.fGLSurface, fUnixWindow.fGLContext);

    /*glXMakeCurrent(fUnixWindow.fDisplay,
                   fUnixWindow.fWin,
                   fUnixWindow.fGLContext);*/
    glViewport(0, 0,
               SkScalarRoundToInt(this->width()),
               SkScalarRoundToInt(this->height()));
    glClearColor(0, 0, 0, 0);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    return true;
}

void SkOSWindow::detach() {
    if (NULL == fUnixWindow.fDisplay || NULL == fUnixWindow.fGLContext) {
        return;
    }
    eglMakeCurrent (eglGetDisplay (wayland.display), EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext (eglGetDisplay (wayland.display), fUnixWindow.fGLContext);
    eglDestroySurface (eglGetDisplay (wayland.display), fUnixWindow.fGLSurface);
    fUnixWindow.fGLContext = EGL_NO_CONTEXT;
    fUnixWindow.fGLSurface = EGL_NO_SURFACE;
    eglTerminate (eglGetDisplay (wayland.display));

    //glXMakeCurrent(fUnixWindow.fDisplay, None, NULL);
    //glXDestroyContext(fUnixWindow.fDisplay, fUnixWindow.fGLContext);
    fUnixWindow.fGLContext = NULL;
}

void SkOSWindow::present() {
    if (wayland.display && fUnixWindow.fGLContext) {
        //glXSwapBuffers(fUnixWindow.fDisplay, fUnixWindow.fWin);
        eglSwapBuffers(eglGetDisplay (wayland.display), fUnixWindow.fGLSurface);
    }
}

void SkOSWindow::onSetTitle(const char title[]) {
    if (NULL == fUnixWindow.fDisplay) {
        return;
    }
    /*XTextProperty textProp;
    textProp.value = (unsigned char*)title;
    textProp.format = 8;
    textProp.nitems = strlen((char*)textProp.value);
    textProp.encoding = XA_STRING;
    XSetWMName(fUnixWindow.fDisplay, fUnixWindow.fWin, &textProp);*/
}

static bool convertBitmapToXImage(XImage& image, const SkBitmap& bitmap) {
    sk_bzero(&image, sizeof(image));

    int bitsPerPixel = bitmap.bytesPerPixel() * 8;
    image.width = bitmap.width();
    image.height = bitmap.height();
    image.format = ZPixmap;
    image.data = (char*) bitmap.getPixels();
    image.byte_order = LSBFirst;
    image.bitmap_unit = bitsPerPixel;
    image.bitmap_bit_order = LSBFirst;
    image.bitmap_pad = bitsPerPixel;
    image.depth = 24;
    image.bytes_per_line = bitmap.rowBytes() - bitmap.width() * 4;
    image.bits_per_pixel = bitsPerPixel;
    return XInitImage(&image);
}

void SkOSWindow::doPaint() {
    if (NULL == fUnixWindow.fDisplay) {
        return;
    }
    // If we are drawing with GL, we don't need XPutImage.
    if (fUnixWindow.fGLContext) {
        return;
    }
    // Draw the bitmap to the screen.
    const SkBitmap& bitmap = getBitmap();
    int width = bitmap.width();
    int height = bitmap.height();

    XImage image;
    if (!convertBitmapToXImage(image, bitmap)) {
        return;
    }

    XPutImage(fUnixWindow.fDisplay,
              fUnixWindow.fWin,
              fUnixWindow.fGc,
              &image,
              0, 0,     // src x,y
              0, 0,     // dst x,y
              width, height);
}

///////////////////////////////////////////////////////////////////////////////

void SkEvent::SignalNonEmptyQueue() {
    // nothing to do, since we spin on our event-queue, polling for XPending
}

void SkEvent::SignalQueueTimer(SkMSec delay) {
    // just need to record the delay time. We handle waking up for it in
    // MyXNextEventWithDelay()
    gTimerDelay = delay;
}
