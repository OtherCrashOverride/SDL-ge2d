#include "../../SDL_internal.h"

#ifndef _SDL_maliblitter_h
#define _SDL_maliblitter_h

#if SDL_VIDEO_DRIVER_MALI && SDL_VIDEO_OPENGL_EGL

#include "../SDL_sysvideo.h"
#include "../SDL_egl_c.h"

#include "SDL_egl.h"
#include "SDL_opengl.h"

typedef struct MALI_Blitter {
    /* OpenGL Surface and Context */
    EGLSurface *surface;
    SDL_GLContext *context;
    GLuint frag, vert, prog, vbo, vao;
    GLint loc_aVertCoord, loc_aTexCoord, loc_uFBOtex, loc_uProj;
    GLsizei viewport_width, viewport_height;
    GLint plane_width, plane_height, plane_pitch;

    struct {
        int fd;
        GLuint texture;
        EGLImageKHR image;
    } planes[3];

    #define SDL_PROC(ret,func,params) ret (APIENTRY *func) params;
    #include "SDL_maliblitter_funcs.h"
    #undef SDL_PROC
} MALI_Blitter;

int MALI_InitBlitter(_THIS, MALI_Blitter *blitter, NativeWindowType nw, int rotation);
void MALI_Blitter_Blit(_THIS, MALI_Blitter *blitter, int texture);
void MALI_TripleBufferInit(SDL_WindowData *windowdata);
void MALI_TripleBufferStop(_THIS);
void MALI_TripleBufferQuit(_THIS);
int MALI_TripleBufferingThread(void *data);

#endif /* SDL_VIDEO_DRIVER_MALI && SDL_VIDEO_OPENGL_EGL */

#endif
