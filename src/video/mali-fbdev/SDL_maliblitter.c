#include "../../SDL_internal.h"

#if SDL_VIDEO_DRIVER_MALI && SDL_VIDEO_OPENGL_EGL

#include "SDL_egl.h"
#include "SDL_opengl.h"

#include "SDL_maliblitter.h"
#include "SDL_maliopengles.h"
#include "SDL_malivideo.h"

static GLchar* blit_vert_fmt =
"#version 100\n"
"varying vec2 vTexCoord;\n"
"attribute vec2 aCoord;\n"
"void main() {\n"
"   vTexCoord = ((aCoord + 1.0) / 2.0);\n"
"   %s\n"
"   gl_Position = vec4(aCoord, 0.0, 1.0);\n"
"}";

static GLchar* blit_frag =
"#version 100\n"
"precision mediump float;"
"varying vec2 vTexCoord;\n"
"uniform sampler2D uFBOTex;\n"
"void main() {\n"
"   gl_FragColor = texture2D(uFBOTex, vTexCoord);\n"
"}\n";

SDL_GLContext
MALI_Blitter_CreateContext(_THIS, EGLSurface egl_surface)
{
    EGLContext egl_context;
    /* max 14 values plus terminator. */
    EGLint attribs[15];
    int attr = 0;

    if (!_this->egl_data) {
        SDL_SetError("EGL not initialized");
        return NULL;
    }

    if (SDL_EGL_HasExtension(_this, SDL_EGL_DISPLAY_EXTENSION, "EGL_KHR_create_context")) {
        attribs[attr++] = EGL_CONTEXT_MAJOR_VERSION_KHR;
        attribs[attr++] = 2;
        attribs[attr++] = EGL_CONTEXT_MINOR_VERSION_KHR;
        attribs[attr++] = 0;

        /* SDL flags match EGL flags. */
        if (_this->gl_config.flags != 0) {
            attribs[attr++] = EGL_CONTEXT_FLAGS_KHR;
            attribs[attr++] = _this->gl_config.flags;
        }
    }

    attribs[attr++] = EGL_NONE;
    
    _this->egl_data->eglBindAPI(_this->egl_data->apitype);
    egl_context = _this->egl_data->eglCreateContext(_this->egl_data->egl_display,
                                      _this->egl_data->egl_config,
                                      EGL_NO_CONTEXT, attribs);

    if (egl_context == EGL_NO_CONTEXT) {
        SDL_EGL_SetError("Could not create EGL context", "eglCreateContext");
        return NULL;
    }

    return (SDL_GLContext) egl_context;
}

#define fourcc_code(a, b, c, d) ((__u32)(a) | ((__u32)(b) << 8) | \
				 ((__u32)(c) << 16) | ((__u32)(d) << 24))

int
MALI_InitBlitter(_THIS, MALI_Blitter *blitter)
{
    GLchar msg[2048] = {}, blit_vert[2048] = {};
    const GLchar *sources[2] = { blit_vert, blit_frag };
    SDL_WindowData *windowdata;
    SDL_DisplayData *displaydata;
    
    windowdata = (SDL_WindowData *)_this->windows->driverdata;
    displaydata = SDL_GetDisplayDriverData(0);

    /* Attempt to initialize necessary functions */
    #define SDL_PROC(ret,func,params) \
        blitter->func = SDL_GL_GetProcAddress(#func); \
        if (!blitter->func) \
        { \
            return 0; \
        }
    #include "SDL_maliblitter_funcs.h"
    #undef SDL_PROC

    blitter->surface = SDL_EGL_CreateSurface(_this, (NativeWindowType) &displaydata->native_display);
    if (blitter->surface == EGL_NO_SURFACE) {
        SDL_EGL_SetError("Failed to setup blitter EGL Surface", "SDL_EGL_CreateSurface");
        return 0;
    }

    blitter->context = MALI_Blitter_CreateContext(_this, blitter->surface);
    if (blitter->context == EGL_NO_CONTEXT) {
        SDL_EGL_SetError("Failed to setup blitter EGL Context", "SDL_EGL_CreateContext");
        return 0;
    }
    
    if (!_this->egl_data->eglMakeCurrent(_this->egl_data->egl_display,
        blitter->surface, 
        blitter->surface, 
        blitter->context))
    {
        SDL_EGL_SetError("Unable to make blitter EGL context current", "eglMakeCurrent");
        return 0;
    }

    /* Setup vertex shader coord orientation */
    SDL_snprintf(blit_vert, sizeof(blit_vert), blit_vert_fmt, 
        (displaydata->rotation == 0) ? "" :
        (displaydata->rotation == 1) ? "vTexCoord = vec2(vTexCoord.y, -vTexCoord.x);" :
        (displaydata->rotation == 2) ? "vTexCoord = vec2(-vTexCoord.x, -vTexCoord.y);" :
        (displaydata->rotation == 3) ? "vTexCoord = vec2(-vTexCoord.y, vTexCoord.x);" :
        "#error Orientation out of scope");

    /* Compile vertex shader */
    blitter->vert = blitter->glCreateShader(GL_VERTEX_SHADER);
    blitter->glShaderSource(blitter->vert, 1, &sources[0], NULL);
    blitter->glCompileShader(blitter->vert);
    blitter->glGetShaderInfoLog(blitter->vert, sizeof(msg), NULL, msg);
    SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, "mali-fbdev: Blitter Vertex Shader Info: %s\n", msg);

    /* Compile the fragment shader */
    blitter->frag = blitter->glCreateShader(GL_FRAGMENT_SHADER);
    blitter->glShaderSource(blitter->frag, 1, &sources[1], NULL);
    blitter->glCompileShader(blitter->frag);
    blitter->glGetShaderInfoLog(blitter->frag, sizeof(msg), NULL, msg);
    SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, "mali-fbdev: Blitter Fragment Shader Info: %s\n", msg);

    blitter->prog = blitter->glCreateProgram();
    blitter->glAttachShader(blitter->prog, blitter->vert);
    blitter->glAttachShader(blitter->prog, blitter->frag);

    blitter->glLinkProgram(blitter->prog);
    blitter->loc_aCoord = blitter->glGetAttribLocation(blitter->prog, "aCoord");
    blitter->loc_uFBOtex = blitter->glGetUniformLocation(blitter->prog, "uFBOTex");
    blitter->glGetProgramInfoLog(blitter->prog, sizeof(msg), NULL, msg);
    SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, "mali-fbdev: Blitter Program Info: %s\n", msg);

    /* Setup programs */
    blitter->glUseProgram(blitter->prog);
    blitter->glUniform1i(blitter->loc_uFBOtex, 0);

    /* Setup for blitting */
    /* Setup viewport */
    blitter->glViewport(0, 0, displaydata->vinfo.xres, displaydata->vinfo.yres);

    /* Generate buffers */
    blitter->glGenBuffers(1, &blitter->vbo);
    blitter->glGenVertexArraysOES(1, &blitter->vao);

    /* Populate buffers */
    blitter->glBindVertexArrayOES(blitter->vao);
    blitter->glBindBuffer(GL_ARRAY_BUFFER, blitter->vbo);
    blitter->glEnableVertexAttribArray(blitter->loc_aCoord);
    blitter->glVertexAttribPointer(blitter->loc_aCoord, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), 0);

    for (int i = 0; i < 3; i++) {
        MALI_EGL_Surface *mali_surf = &windowdata->surface[0];

        /* 
         * There's a nasty bug in EmuELEC 4.5 libMali which causes eglCreateImageKHR to expect
         * ints instead of EGLAttrib (aka intptr_t) - check 0x004ddb84 for answers.
         */

        EGLint attribute_list[] = {
            EGL_WIDTH, mali_surf->pixmap.width,
            EGL_HEIGHT, mali_surf->pixmap.height,
            EGL_LINUX_DRM_FOURCC_EXT, fourcc_code('A', 'R', '2', '4'),
            EGL_DMA_BUF_PLANE0_FD_EXT, windowdata->surface[i].shared_fd,
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
            EGL_DMA_BUF_PLANE0_PITCH_EXT, mali_surf->pixmap.planes[0].stride,
            EGL_NONE
        };

        blitter->frames[i].image = _this->egl_data->eglCreateImageKHR(
            _this->egl_data->egl_display,
            EGL_NO_CONTEXT,
            EGL_LINUX_DMA_BUF_EXT,
            (EGLClientBuffer)NULL,
            &attribute_list[0]);
        if (blitter->frames[i].image == EGL_NO_IMAGE_KHR) {
            SDL_EGL_SetError("Failed to create Blitter EGL Image", "eglCreateImageKHR");
            return 0;
        }

        blitter->glGenTextures(1, &blitter->frames[i].texture);
        blitter->glActiveTexture(GL_TEXTURE0);
        blitter->glBindTexture(GL_TEXTURE_2D, blitter->frames[i].texture);
        blitter->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        blitter->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        blitter->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, blitter->frames[i].image);
    }

    return 1;
}

void MALI_Blitter_Blit(_THIS, MALI_Blitter *blitter, int texture)
{
    static GLfloat vert[] = {
        -1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f
    };

    blitter->glBindVertexArrayOES(blitter->vao);
    blitter->glBindTexture(GL_TEXTURE_2D, blitter->frames[texture].texture);
    blitter->glBufferData(GL_ARRAY_BUFFER, sizeof(vert), vert, GL_STATIC_DRAW);
    blitter->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

#endif /* SDL_VIDEO_DRIVER_MALI */