#include "../../SDL_internal.h"

//#if SDL_VIDEO_DRIVER_MALI

/* SDL internals */
#include "../SDL_sysvideo.h"
#include "SDL_version.h"
#include "SDL_syswm.h"
#include "SDL_loadso.h"
#include "SDL_events.h"
#include "../../events/SDL_events_c.h"

#ifdef SDL_INPUT_LINUXEV
#include "../../core/linux/SDL_evdev.h"
#endif

#include "SDL_malivideo.h"
#include "SDL_maliopengles.h"

#include <drm/drm_fourcc.h>

void
MALI_GLES_DefaultProfileConfig(_THIS, int *mask, int *major, int *minor)
{
    if (!SDL_getenv("SDL_DEFAULT_CONTEXT_PROFILE"))
    {
        *mask = SDL_GL_CONTEXT_PROFILE_ES;
        *major = 2;
        *minor = 0;
    }
}

static void
MALI_Destroy(SDL_VideoDevice * device)
{
    if (device->driverdata != NULL) {
        SDL_free(device->driverdata);
        device->driverdata = NULL;
    }
}

static SDL_VideoDevice *
MALI_Create()
{
    SDL_VideoDevice *device;

    /* Initialize SDL_VideoDevice structure */
    device = (SDL_VideoDevice *) SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (device == NULL) {
        SDL_OutOfMemory();
        return NULL;
    }

    device->driverdata = NULL;

    /* Setup amount of available displays and current display */
    device->num_displays = 0;

    /* Set device free function */
    device->free = MALI_Destroy;

    /* Setup all functions which we can handle */
    device->VideoInit = MALI_VideoInit;
    device->VideoQuit = MALI_VideoQuit;
    device->GetDisplayModes = MALI_GetDisplayModes;
    device->SetDisplayMode = MALI_SetDisplayMode;
    device->CreateSDLWindow = MALI_CreateWindow;
    device->SetWindowTitle = MALI_SetWindowTitle;
    device->SetWindowPosition = MALI_SetWindowPosition;
    device->SetWindowSize = MALI_SetWindowSize;
    device->ShowWindow = MALI_ShowWindow;
    device->HideWindow = MALI_HideWindow;
    device->DestroyWindow = MALI_DestroyWindow;
    device->GetWindowWMInfo = MALI_GetWindowWMInfo;

    device->GL_LoadLibrary = MALI_GLES_LoadLibrary;
    device->GL_GetProcAddress = MALI_GLES_GetProcAddress;
    device->GL_UnloadLibrary = MALI_GLES_UnloadLibrary;
    device->GL_CreateContext = MALI_GLES_CreateContext;
    device->GL_MakeCurrent = MALI_GLES_MakeCurrent;
    device->GL_SetSwapInterval = MALI_GLES_SetSwapInterval;
    device->GL_GetSwapInterval = MALI_GLES_GetSwapInterval;
    device->GL_SwapWindow = MALI_GLES_SwapWindow;
    device->GL_DeleteContext = MALI_GLES_DeleteContext;

    device->GL_DefaultProfileConfig = MALI_GLES_DefaultProfileConfig;

    device->PumpEvents = MALI_PumpEvents;

    return device;
}

VideoBootStrap MALI_bootstrap = {
    "mali",
    "Mali EGL Video Driver",
    MALI_Create
};

/*****************************************************************************/
/* SDL Video and Display initialization/handling functions                   */
/*****************************************************************************/

int
MALI_VideoInit(_THIS)
{
    SDL_VideoDisplay display;
    SDL_DisplayMode current_mode;
    SDL_DisplayData *data;

    data = (SDL_DisplayData *) SDL_calloc(1, sizeof(SDL_DisplayData));
    if (data == NULL) {
        return SDL_OutOfMemory();
    }

    data->disp = gou_display_create();

    data->native_display.width = gou_display_width_get(data->disp);
    data->native_display.height = gou_display_height_get(data->disp);

    SDL_zero(current_mode);
    current_mode.w = data->native_display.width;
    current_mode.h = data->native_display.height;
    /* FIXME: Is there a way to tell the actual refresh rate? */
    current_mode.refresh_rate = 60;
    /* 32 bpp for default */
    //current_mode.format = SDL_PIXELFORMAT_ABGR8888;
    current_mode.format = SDL_PIXELFORMAT_RGBX8888;

    current_mode.driverdata = NULL;

    SDL_zero(display);
    display.desktop_mode = current_mode;
    display.current_mode = current_mode;
    display.driverdata = data;

    SDL_AddVideoDisplay(&display, SDL_FALSE);

#ifdef SDL_INPUT_LINUXEV
    if (SDL_EVDEV_Init() < 0) {
        return -1;
    }
#endif

    return 0;
}

void
MALI_VideoQuit(_THIS)
{
    SDL_DisplayData *displaydata = (SDL_DisplayData*)_this->driverdata;

    gou_display_destroy(displaydata->disp);

#ifdef SDL_INPUT_LINUXEV
    SDL_EVDEV_Quit();
#endif

}

void
MALI_GetDisplayModes(_THIS, SDL_VideoDisplay * display)
{
    /* Only one display mode available, the current one */
    SDL_AddDisplayMode(display, &display->current_mode);
}

int
MALI_SetDisplayMode(_THIS, SDL_VideoDisplay * display, SDL_DisplayMode * mode)
{
    return 0;
}

static MALI_Surface MALI_EGL_CreatePixmapSurface(_THIS, SDL_WindowData *windowdata, SDL_DisplayData *displaydata) 
{
    MALI_Surface surface;
    int w;
    int h;
    mali_pixmap pixmap;

    memset(&surface, 0, sizeof(surface));
    surface.egl_surface = EGL_NO_SURFACE;

    _this->egl_data->egl_surfacetype = EGL_PIXMAP_BIT;
    if (SDL_EGL_ChooseConfig(_this) != 0) {
        return surface;
    }

    if (_this->gl_config.framebuffer_srgb_capable) {
        {
            SDL_SetError("EGL implementation does not support sRGB system framebuffers");
            return surface;
        }
    }

    w = displaydata->native_display.width;
    h = displaydata->native_display.height;

    surface.gou_surface = gou_surface_create(displaydata->disp, w, h, DRM_FORMAT_ARGB8888);

    // Populate pixmap definitions
    memset(&pixmap, 0, sizeof(pixmap));

    pixmap.width = w;
    pixmap.height = h;
    pixmap.format = MALI_FORMAT_ARGB8888; // appears to be 888X

    pixmap.planes[0].stride = gou_surface_stride_get(surface.gou_surface);
    pixmap.planes[0].size = 
    pixmap.planes[0].stride * pixmap.height;
    pixmap.planes[0].offset = 0;
    pixmap.handles[0] = gou_surface_share_fd(surface.gou_surface);


    surface.pixmap_handle = displaydata->egl_create_pixmap_ID_mapping(&pixmap);
    SDL_Log("Created pixmap handle %p\n", (void*)surface.pixmap_handle);
    
    surface.egl_surface = _this->egl_data->eglCreatePixmapSurface(
            _this->egl_data->egl_display,
            _this->egl_data->egl_config,
            surface.pixmap_handle, NULL);
    if (surface.egl_surface == EGL_NO_SURFACE) {
        SDL_EGL_SetError("unable to create an EGL window surface", "eglCreatePixmapSurface");
    }

    return surface;
}

static void MALI_EGL_DestroyPixmapSurface(_THIS, SDL_WindowData *windowdata, SDL_DisplayData *displaydata, MALI_Surface* surface) 
{
    _this->egl_data->eglDestroySurface(_this->egl_data->egl_display, surface->egl_surface);
    // TODO - Destroy native pixmap handle
    gou_surface_destroy(surface->gou_surface);
}

int
MALI_CreateWindow(_THIS, SDL_Window * window)
{
    SDL_WindowData *windowdata;
    SDL_DisplayData *displaydata;
    int i;

    displaydata = SDL_GetDisplayDriverData(0);

    /* Allocate window internal data */
    windowdata = (SDL_WindowData *) SDL_calloc(1, sizeof(SDL_WindowData));
    if (windowdata == NULL) {
        return SDL_OutOfMemory();
    }

    /* Windows have one size for now */
    window->w = displaydata->native_display.width;
    window->h = displaydata->native_display.height;

    /* OpenGL ES is the law here */
    window->flags |= SDL_WINDOW_OPENGL;

    if (!_this->egl_data) {
        if (SDL_GL_LoadLibrary(NULL) < 0) {
            return -1;
        }
    }

    /* Acquire handle to internal pixmap routines */
    if (!displaydata->egl_create_pixmap_ID_mapping) {
        displaydata->egl_create_pixmap_ID_mapping = SDL_EGL_GetProcAddress(_this, "egl_create_pixmap_ID_mapping");
        if (!displaydata->egl_create_pixmap_ID_mapping) {
            MALI_VideoQuit(_this);
            return SDL_SetError("mali-fbdev: egl_create_pixmap_ID_mapping not exposed by EGL driver.");
        }
    }

    for (i = 0; i < MALI_MAX_BUFFERS; ++i)
    {
        windowdata->surfaces[i] = MALI_EGL_CreatePixmapSurface(_this, windowdata, displaydata);
    }

    windowdata->egl_surface = windowdata->surfaces[0].egl_surface;
    if (windowdata->egl_surface == EGL_NO_SURFACE) {
        MALI_VideoQuit(_this);
        return SDL_SetError("mali-fbdev: Can't create EGL window surface");
    }

    /* Setup driver data for this window */
    window->driverdata = windowdata;

    /* One window, it always has focus */
    SDL_SetMouseFocus(window);
    SDL_SetKeyboardFocus(window);

    /* Window has been successfully created */
    return 0;
}

void
MALI_DestroyWindow(_THIS, SDL_Window * window)
{
    SDL_WindowData *data;
    int i;

    data = window->driverdata;
    if (data) {
        if (data->egl_surface != EGL_NO_SURFACE) {
            SDL_EGL_DestroySurface(_this, data->egl_surface);
            data->egl_surface = EGL_NO_SURFACE;
        }

        for (i = 0; i < MALI_MAX_BUFFERS; ++i)
        {
            MALI_EGL_DestroyPixmapSurface(_this, data, SDL_GetDisplayDriverData(0), &data->surfaces[i]);
        }

        SDL_free(data);
    }
    window->driverdata = NULL;
}

void
MALI_SetWindowTitle(_THIS, SDL_Window * window)
{
}

void
MALI_SetWindowPosition(_THIS, SDL_Window * window)
{
}

void
MALI_SetWindowSize(_THIS, SDL_Window * window)
{
}

void
MALI_ShowWindow(_THIS, SDL_Window * window)
{
}

void
MALI_HideWindow(_THIS, SDL_Window * window)
{
}

/*****************************************************************************/
/* SDL Window Manager function                                               */
/*****************************************************************************/
SDL_bool
MALI_GetWindowWMInfo(_THIS, SDL_Window * window, struct SDL_SysWMinfo *info)
{
    if (info->version.major <= SDL_MAJOR_VERSION) {
        return SDL_TRUE;
    } else {
        SDL_SetError("application not compiled with SDL %d.%d\n",
            SDL_MAJOR_VERSION, SDL_MINOR_VERSION);
    }

    /* Failed to get window manager information */
    return SDL_FALSE;
}

/*****************************************************************************/
/* SDL event functions                                                       */
/*****************************************************************************/
void MALI_PumpEvents(_THIS)
{
#ifdef SDL_INPUT_LINUXEV
    SDL_EVDEV_Poll();
#endif
}

//#endif /* SDL_VIDEO_DRIVER_MALI */

