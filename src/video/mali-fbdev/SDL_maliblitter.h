#include "../../SDL_internal.h"

#ifndef _SDL_maliblitter_h
#define _SDL_maliblitter_h

#if SDL_VIDEO_DRIVER_MALI && SDL_VIDEO_OPENGL_EGL

#include "../SDL_sysvideo.h"
#include "../SDL_egl_c.h"

#include "SDL_egl.h"
#include "SDL_opengl.h"

typedef struct MALI_Blitter {
    EGLSurface *surface;
    SDL_GLContext *context;
    GLuint frag, vert, prog, vbo, vao;
    GLint loc_aCoord, loc_uFBOtex;
    struct {
        GLuint texture;
        EGLImageKHR image;
    } frames[3];

    #define SDL_PROC(ret,func,params) ret (APIENTRY *func) params;
    #include "SDL_maliblitter_funcs.h"
    #undef SDL_PROC
} MALI_Blitter;

extern int MALI_InitBlitter(_THIS, MALI_Blitter *blitter);
extern void MALI_Blitter_Blit(_THIS, MALI_Blitter *blitter, int texture);

#endif /* SDL_VIDEO_DRIVER_MALI && SDL_VIDEO_OPENGL_EGL */

#endif
