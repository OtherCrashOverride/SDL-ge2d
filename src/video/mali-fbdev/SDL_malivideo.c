#include "../../SDL_internal.h"

#if SDL_VIDEO_DRIVER_MALI

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
    struct fb_var_screeninfo vinfo;
    int fd;
    SDL_VideoDisplay display;
    SDL_DisplayMode current_mode;
    SDL_DisplayData *data;

    data = (SDL_DisplayData *) SDL_calloc(1, sizeof(SDL_DisplayData));
    if (data == NULL) {
        return SDL_OutOfMemory();
    }

    fd = open("/dev/fb0", O_RDWR, 0);
    if (fd < 0) {
        return SDL_SetError("mali-fbdev: Could not open framebuffer device");
    }

    data->ion_fd = open("/dev/ion", O_RDWR, 0);
    if (data->ion_fd < 0) {
        return SDL_SetError("mali-fbdev: Could not open ion device");
    }

    data->ge2d_fd = open("/dev/ge2d", O_RDWR, 0);
    if (data->ge2d_fd < 0) {
        close(data->ion_fd);
        return SDL_SetError("mali-fbdev: Could not open ge2d device");
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        MALI_VideoQuit(_this);
        return SDL_SetError("mali-fbdev: Could not get framebuffer information");
    }
    /* Enable triple buffering */
    /*
    vinfo.yres_virtual = vinfo.yres * 3;
    if (ioctl(fd, FBIOPUT_VSCREENINFO, vinfo) == -1) {
	printf("mali-fbdev: Error setting VSCREENINFO\n");
    }
    */
    close(fd);
    system("setterm -cursor off");

    data->native_display.width = vinfo.xres;
    data->native_display.height = vinfo.yres;

    SDL_zero(current_mode);
    current_mode.w = vinfo.xres;
    current_mode.h = vinfo.yres;
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
    SDL_DisplayData *displaydata = (SDL_DisplayData*)SDL_GetDisplayDriverData(0);
    int fd = open("/dev/tty", O_RDWR);

    /* Cleanup after ion and ge2d */
    close(displaydata->ion_fd);
    close(displaydata->ge2d_fd);
    //TODO:: Destroy the other buffers...

    /* Clear the framebuffer and ser cursor on again */
    ioctl(fd, VT_ACTIVATE, 5);
    ioctl(fd, VT_ACTIVATE, 1);
    close(fd);
    system("setterm -cursor on");

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

static EGLSurface *MALI_EGL_CreatePixmapSurface(_THIS, SDL_WindowData *windowdata, SDL_DisplayData *displaydata) 
{
    struct ion_fd_data ion_data;
    struct ion_allocation_data allocation_data;
    int i, io;
    EGLSurface *surface;

    _this->egl_data->egl_surfacetype = EGL_PIXMAP_BIT;
    if (SDL_EGL_ChooseConfig(_this) != 0) {
        return EGL_NO_SURFACE;
    }

    if (_this->gl_config.framebuffer_srgb_capable) {
        {
            SDL_SetError("EGL implementation does not support sRGB system framebuffers");
            return EGL_NO_SURFACE;
        }
    }

    // Populate pixmap definitions
    windowdata->pixmap.width = displaydata->native_display.width;
    windowdata->pixmap.height = displaydata->native_display.height;
    for (i = 0; i < 3; i++)
    {
        windowdata->pixmap.planes[i].stride = MALI_ALIGN(windowdata->pixmap.width * 4, 64);
        windowdata->pixmap.planes[i].size = 
            windowdata->pixmap.planes[i].stride * windowdata->pixmap.height;
        windowdata->pixmap.planes[i].offset = 0;
        windowdata->pixmap.format = MALI_FORMAT_ARGB8888; // appears to be 888X

        allocation_data = (struct ion_allocation_data){
            .len = windowdata->pixmap.planes[i].size,
            .heap_id_mask = (1 << ION_HEAP_TYPE_DMA),
            .flags = 0
        };

        io = ioctl(displaydata->ion_fd, ION_IOC_ALLOC, &allocation_data);
        if (io != 0)
        {
            SDL_EGL_SetError("Unable to create backing ION buffers", "ION_IOC_ALLOC");
            return EGL_NO_SURFACE;
        }

        ion_data = (struct ion_fd_data){
            .handle = allocation_data.handle
        };

        io = ioctl(displaydata->ion_fd, ION_IOC_SHARE, &ion_data);
        if (io != 0)
        {
            SDL_EGL_SetError("Unable to create backing ION buffers", "ION_IOC_SHARE");
            return EGL_NO_SURFACE;
        }

        windowdata->ion_surface[i].handle = allocation_data.handle;
        windowdata->ion_surface[i].shared_fd = ion_data.fd;
        windowdata->pixmap.handles[i] = ion_data.fd;
    }

    windowdata->pixmap_handle = displaydata->egl_create_pixmap_ID_mapping(&windowdata->pixmap);
    SDL_Log("Created pixmap handle %p\n", windowdata->pixmap_handle);
    
    surface = _this->egl_data->eglCreatePixmapSurface(
            _this->egl_data->egl_display,
            _this->egl_data->egl_config,
            windowdata->pixmap_handle, NULL);
    if (surface == EGL_NO_SURFACE) {
        SDL_EGL_SetError("unable to create an EGL window surface", "eglCreatePixmapSurface");
    }

    return surface;
}

int
MALI_CreateWindow(_THIS, SDL_Window * window)
{
    SDL_WindowData *windowdata;
    SDL_DisplayData *displaydata;
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
        displaydata->egl_destroy_pixmap_ID_mapping = SDL_EGL_GetProcAddress(_this, "egl_destroy_pixmap_ID_mapping");
        if (!displaydata->egl_create_pixmap_ID_mapping || 
            !displaydata->egl_destroy_pixmap_ID_mapping) {
            MALI_VideoQuit(_this);
            return SDL_SetError("mali-fbdev: One or more required libmali internal not exposed.");
        }
    }

    windowdata->egl_surface = MALI_EGL_CreatePixmapSurface(_this, windowdata, displaydata);
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
    int i, io;
    SDL_WindowData *windowdata;
    SDL_DisplayData *displaydata;
    struct ion_handle_data ionHandleData;

    SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, "Destroying MALI window.");

    windowdata = window->driverdata;
    displaydata = SDL_GetDisplayDriverData(0);

    for (i = 0; i < 3; i++) {
        close(windowdata->ion_surface[i].shared_fd);
        ionHandleData = (struct ion_handle_data){
            .handle = windowdata->ion_surface[i].handle
        };

        io = ioctl(displaydata->ion_fd, ION_IOC_FREE, &ionHandleData);
        if (io != 0)
        {
            SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "ION_IOC_FREE failed.");
        }

        windowdata->ion_surface[i].shared_fd = -1;
        windowdata->ion_surface[i].handle = 0;
    }

    if (windowdata) {
        if (windowdata->egl_surface != EGL_NO_SURFACE) {
            SDL_EGL_DestroySurface(_this, windowdata->egl_surface);
            windowdata->egl_surface = EGL_NO_SURFACE;
        }
        SDL_free(windowdata);
    }

    displaydata->egl_destroy_pixmap_ID_mapping((unsigned long)windowdata->pixmap_handle);
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

#endif /* SDL_VIDEO_DRIVER_MALI */

