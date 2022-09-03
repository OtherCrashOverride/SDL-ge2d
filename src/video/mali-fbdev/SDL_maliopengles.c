#include "../../SDL_internal.h"


#if SDL_VIDEO_DRIVER_MALI && SDL_VIDEO_OPENGL_EGL

#include "SDL.h"

#include "SDL_maliopengles.h"
#include "SDL_malivideo.h"
#include "SDL_maliblitter.h"

int MALI_TripleBufferingThread(void *data)
{
    unsigned int page;
    MALI_Blitter blitter = {};
    MALI_EGL_Surface *current_surface;
	SDL_WindowData *windowdata;
    SDL_DisplayData *displaydata;
    SDL_VideoDevice* _this;
    
    _this = (SDL_VideoDevice*)data;
    windowdata = (SDL_WindowData *)_this->windows->driverdata;
    displaydata = (SDL_DisplayData*)SDL_GetDisplayDriverData(0);

    /* Initialize blitter */
    if (!MALI_InitBlitter(_this, &blitter))
    {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Failed to create blitter thread context");
        SDL_Quit();
    }

    /* Reset yoffset, otherwise applications can get stuck */
    displaydata->vinfo.yoffset = 0;
    if (ioctl(displaydata->fb_fd, FBIOPUT_VSCREENINFO, &displaydata->vinfo) < 0) {
        MALI_VideoQuit(_this);
        return SDL_SetError("mali-fbdev: Could not put framebuffer information");
    }

    /* Signal triplebuf available */
	SDL_LockMutex(windowdata->triplebuf_mutex);
	SDL_CondSignal(windowdata->triplebuf_cond);

	for (;;) {
        SDL_CondWait(windowdata->triplebuf_cond, windowdata->triplebuf_mutex);
		if (windowdata->triplebuf_thread_stop)
			break;

		/* Flip the most recent back buffer with the front buffer */
		page = windowdata->current_page;
		windowdata->current_page = windowdata->new_page;
		windowdata->new_page = page;

        /* select surface to wait and blit */
        current_surface = &windowdata->surface[windowdata->current_page];

		/* wait for fence and flip display */
        if (_this->egl_data->eglClientWaitSyncKHR(
            _this->egl_data->egl_display,
            current_surface->fence, 
            EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, 
            EGL_FOREVER_NV))
        {
            blitter.glClearColor(0.0, 1.0, 0.0, 1.0);
            blitter.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            MALI_Blitter_Blit(_this, &blitter, windowdata->current_page);
            _this->egl_data->eglSwapBuffers(_this->egl_data->egl_display, blitter.surface);
        }
	}

    _this->egl_data->eglMakeCurrent(_this->egl_data->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    _this->egl_data->eglDestroySurface(_this->egl_data->egl_display, blitter.surface);
    _this->egl_data->eglDestroyContext(_this->egl_data->egl_display, blitter.context);

	SDL_UnlockMutex(windowdata->triplebuf_mutex);
	return 0;
}

void MALI_TripleBufferInit(SDL_WindowData *windowdata)
{
	windowdata->triplebuf_mutex = SDL_CreateMutex();
	windowdata->triplebuf_cond = SDL_CreateCond();
	windowdata->triplebuf_thread = NULL;
}

void MALI_TripleBufferStop(_THIS)
{
    SDL_WindowData *windowdata = (SDL_WindowData*)_this->windows->driverdata;
    if (!windowdata || windowdata->triplebuf_thread == NULL)
        return;

    SDL_LockMutex(windowdata->triplebuf_mutex);
    windowdata->triplebuf_thread_stop = 1;
    SDL_CondSignal(windowdata->triplebuf_cond);
    SDL_UnlockMutex(windowdata->triplebuf_mutex);

    SDL_WaitThread(windowdata->triplebuf_thread, NULL);
    windowdata->triplebuf_thread = NULL;
}

void MALI_TripleBufferQuit(_THIS)
{
    SDL_WindowData *windowdata;
    if (!_this->windows)
        return;

    windowdata = (SDL_WindowData*)_this->windows->driverdata;
    if (!windowdata || windowdata->triplebuf_thread == NULL)
        return;

    MALI_TripleBufferStop(_this);
	SDL_DestroyMutex(windowdata->triplebuf_mutex);
	SDL_DestroyCond(windowdata->triplebuf_cond);
}

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
