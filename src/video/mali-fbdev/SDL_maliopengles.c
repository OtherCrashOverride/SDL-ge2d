#include "../../SDL_internal.h"

#if SDL_VIDEO_DRIVER_MALI && SDL_VIDEO_OPENGL_EGL

#include "SDL_maliopengles.h"
#include "SDL_malivideo.h"

/* EGL implementation of SDL OpenGL support */

int
MALI_GLES_LoadLibrary(_THIS, const char *path)
{
    return SDL_EGL_LoadLibrary(_this, path, EGL_DEFAULT_DISPLAY, 0);
}

int MALI_GLES_SwapWindow(_THIS, SDL_Window * window)
{

    int r = 0;
    SDL_DisplayData *displaydata = SDL_GetDisplayDriverData(0);
    SDL_WindowData *data = window->driverdata;

    static void (*gl_finish)() = NULL;

    if (!gl_finish) {
        gl_finish = SDL_EGL_GetProcAddress(_this, "glFinish");
    }

    gl_finish();

    /*
    void gou_display_present(gou_display_t* display, gou_surface_t* surface,
            int srcX, int srcY, int srcWidth, int srcHeight, bool mirrorX, bool mirrorY,
            int dstX, int dstY, int dstWidth, int dstHeight);
    */
 
    gou_display_present(displaydata->disp, data->surf[0],
            0, 0, gou_surface_width_get(data->surf[0]), gou_surface_height_get(data->surf[0]),
            false, true,
            0, 0, displaydata->native_display.width, displaydata->native_display.height);

    return r;
}

SDL_EGL_CreateContext_impl(MALI)
SDL_EGL_MakeCurrent_impl(MALI)

#endif /* SDL_VIDEO_DRIVER_MALI && SDL_VIDEO_OPENGL_EGL */
