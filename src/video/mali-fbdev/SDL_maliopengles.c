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

static void
MALI_Rotate_Blit(_THIS, SDL_Window *window, int rotation)
{
    int io;
    static struct ge2d_para_s blitRect = {};
    static struct config_para_ex_ion_s blit_config = {};
    SDL_DisplayData *displaydata;
    SDL_WindowData *windowdata = (SDL_WindowData*)_this->windows->driverdata;

    displaydata = SDL_GetDisplayDriverData(0);

    blit_config.alu_const_color = (uint32_t)~0x0;

    // Definitions for the destionation buffer
    blit_config.dst_para.mem_type = CANVAS_OSD0;
    blit_config.dst_para.format = GE2D_FORMAT_S32_ARGB;

    blit_config.dst_para.left = 0;
    blit_config.dst_para.top = 0;
    blit_config.dst_para.width = displaydata->native_display.width;
    blit_config.dst_para.height = displaydata->native_display.height;
    blit_config.dst_para.x_rev = 0;
    blit_config.dst_para.y_rev = 0;

    switch (rotation)
    {
        // OpenGL is flipped...
        case Rotation_0:
            blit_config.dst_para.y_rev = 1;
            break;

        case Rotation_90:
            blit_config.dst_xy_swap = 1;
            blit_config.dst_para.y_rev = 1;
            blit_config.dst_para.x_rev = 1;
            break;

        case Rotation_180:
            blit_config.dst_para.x_rev = 1;
            break;

        case Rotation_270:
            blit_config.dst_xy_swap = 1;
            break;
            
        default:
            break;
    }

    // Definitions for the source buffers
    blit_config.src_para.mem_type = CANVAS_ALLOC;
    blit_config.src_para.format = GE2D_FORMAT_S32_ARGB;

    blit_config.src_para.left = 0;
    blit_config.src_para.top = 0;
    blit_config.src_para.width = window->w;
    blit_config.src_para.height = window->h;

    blit_config.src_planes[0].shared_fd = windowdata->ion_surface[0].shared_fd;
    blit_config.src_planes[0].w = windowdata->pixmap.width;
    blit_config.src_planes[0].h = windowdata->pixmap.height;

    io = ioctl(displaydata->ge2d_fd, GE2D_CONFIG_EX_ION, &blit_config);
    if (io < 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "GE2D_CONFIG failed.");
        abort();
    }

    blitRect.src1_rect.x = 0;
    blitRect.src1_rect.y = 0;
    blitRect.src1_rect.w = windowdata->pixmap.width;
    blitRect.src1_rect.h = windowdata->pixmap.height;

    blitRect.dst_rect.x = 0;
    blitRect.dst_rect.y = 0;
    blitRect.dst_rect.w = blit_config.dst_para.width;
    blitRect.dst_rect.h = blit_config.dst_para.height;

    io = ioctl(displaydata->ge2d_fd, GE2D_STRETCHBLIT_NOALPHA, &blitRect);
    if (io < 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "GE2D Blit failed.");
        abort();
    }
}

int MALI_GLES_SwapWindow(_THIS, SDL_Window * window)
{
    int r;
    static void (*gl_finish)() = NULL;

    if (!gl_finish) {
        gl_finish = SDL_EGL_GetProcAddress(_this, "glFinish");
    }

    gl_finish();
    r = SDL_EGL_SwapBuffers(_this, ((SDL_WindowData *) window->driverdata)->egl_surface);
    MALI_Rotate_Blit(_this, window, Rotation_0);

    return r;
}

SDL_EGL_CreateContext_impl(MALI)
SDL_EGL_MakeCurrent_impl(MALI)

#endif /* SDL_VIDEO_DRIVER_MALI && SDL_VIDEO_OPENGL_EGL */
