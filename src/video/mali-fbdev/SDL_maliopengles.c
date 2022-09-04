#include "../../SDL_internal.h"


#if SDL_VIDEO_DRIVER_MALI && SDL_VIDEO_OPENGL_EGL

#include "SDL.h"

#include "SDL_malivideo.h"
#include "SDL_maliopengles.h"
#include "SDL_maliblitter.h"

int MALI_GLES_LoadLibrary(_THIS, const char *path)
{
    return SDL_EGL_LoadLibrary(_this, path, EGL_DEFAULT_DISPLAY, 0);
}

int MALI_GLES_SwapWindow(_THIS, SDL_Window * window)
{
    int r;
    unsigned int page;
    EGLSurface surf;
    SDL_WindowData *windowdata;

    windowdata = (SDL_WindowData*)_this->windows->driverdata;

    SDL_LockMutex(windowdata->triplebuf_mutex);

    page = windowdata->new_page;
    windowdata->new_page = windowdata->flip_page;
    windowdata->flip_page = page;

    surf = windowdata->surface[windowdata->flip_page].egl_surface;
    r = _this->egl_data->eglMakeCurrent(_this->egl_data->egl_display, surf, surf, _this->current_glctx);
    windowdata->surface[windowdata->new_page].fence = _this->egl_data->eglCreateSyncKHR(_this->egl_data->egl_display, EGL_SYNC_FENCE_KHR, NULL);

    SDL_CondSignal(windowdata->triplebuf_cond);
    SDL_UnlockMutex(windowdata->triplebuf_mutex);

    return r;
}

int
MALI_GLES_MakeCurrent(_THIS, SDL_Window * window, SDL_GLContext context)
{
    SDL_WindowData *windowdata;
    if (window) {
        windowdata = window->driverdata;
        return SDL_EGL_MakeCurrent(_this, windowdata->surface[windowdata->new_page].egl_surface, context);

    } else {
        return SDL_EGL_MakeCurrent(_this, EGL_NO_SURFACE, context);
    }
}

SDL_GLContext
MALI_GLES_CreateContext(_THIS, SDL_Window * window)
{
    SDL_WindowData *windowdata = (SDL_WindowData *)window->driverdata;
    return SDL_EGL_CreateContext(_this, windowdata->surface[windowdata->new_page].egl_surface);
}

#endif /* SDL_VIDEO_DRIVER_MALI && SDL_VIDEO_OPENGL_EGL */
