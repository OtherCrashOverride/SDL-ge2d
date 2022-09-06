#include "../../SDL_internal.h"

#if SDL_VIDEO_DRIVER_MALI && SDL_VIDEO_OPENGL_EGL

#include "SDL.h"
#include "SDL_egl.h"
#include "SDL_opengl.h"

#include "SDL_malivideo.h"
#include "SDL_maliopengles.h"
#include "SDL_maliblitter.h"

/* used to simplify code */
typedef struct mat4 {
    GLfloat v[16];
} mat4;

static GLchar* blit_vert_fmt =
"#version 100\n"
"varying vec2 vTexCoord;\n"
"attribute vec2 aVertCoord;\n"
"attribute vec2 aTexCoord;\n"
"uniform mat4 uProj;"
"uniform vec2 uTexSize;\n"
"void main() {\n"
"   %s\n"
"   %s\n"
"   gl_Position = uProj * vec4(aVertCoord, 0.0, 1.0);\n"
"}";

static GLchar* blit_frag_standard =
"#version 100\n"
"precision mediump float;"
"varying vec2 vTexCoord;\n"
"uniform sampler2D uFBOTex;\n"
"uniform vec2 uTexSize;\n"
"uniform vec2 uScale;\n"
"void main() {\n"
"   vec2 texel_floored = floor(vTexCoord);\n"
"   gl_FragColor = texture2D(uFBOTex, vTexCoord);\n"
"}\n";

// Ported from TheMaister's sharp-bilinear-simple.slang
static GLchar* blit_frag_hq =
"#version 100\n"
"precision mediump float;"
"varying vec2 vTexCoord;\n"
"uniform sampler2D uFBOTex;\n"
"uniform vec2 uTexSize;\n"
"uniform vec2 uScale;\n"
"void main() {\n"
"   vec2 texel_floored = floor(vTexCoord);\n"
"   vec2 s = fract(vTexCoord);\n"
"   vec2 region_range = 0.5 - 0.5 / uScale;\n"
"   vec2 center_dist = s - 0.5;\n"
"   vec2 f = (center_dist - clamp(center_dist, -region_range, region_range)) * uScale + 0.5;\n"
"   vec2 mod_texel = texel_floored + f;\n"
"   gl_FragColor = texture2D(uFBOTex, mod_texel / uTexSize);\n"
"}\n";

SDL_GLContext
MALI_Blitter_CreateContext(_THIS, EGLSurface egl_surface)
{
    EGLContext egl_context;
    /* max 14 values plus terminator. */
    EGLint attribs[15];
    int attr = 0;

    if (!_this->egl_data) {
        SDL_SetError("mali-fbdev: EGL not initialized");
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
        SDL_EGL_SetError("mali-fbdev: Could not create EGL context", "eglCreateContext");
        return NULL;
    }

    return (SDL_GLContext) egl_context;
}

static void
get_aspect_correct_coords(int viewport[2], int plane[2], int rotation, GLfloat vert[4][4], GLfloat scale[2])
{
    float aspect_plane, aspect_viewport, ratio_x, ratio_y;
    int shift_x, shift_y, temp;
    aspect_plane = (float)plane[0] / plane[1];
    aspect_viewport = (float)viewport[0] / viewport[1];

    // when sideways, invert plane coords
    if (rotation & 1) {
        temp = plane[0];
        plane[0] = plane[1];
        plane[1] = temp;
    }

    if (aspect_viewport > aspect_plane) {
        // viewport wider than plane
        ratio_x = plane[0] * ((float)viewport[1] / plane[1]);
        ratio_y = viewport[1];
        shift_x = (viewport[0] - ratio_x) / 2.0f;
        shift_y = 0;
    } else {
        // plane wider than viewport
        ratio_x = viewport[0];
        ratio_y = plane[1] * ((float)viewport[0] / plane[0]);
        shift_x = 0;
        shift_y = (viewport[1] - ratio_y) / 2.0f;
    }

    // Instead of normalized UVs, use full texture size.
    vert[0][2] = (int)(0.0f * plane[0]); vert[0][3] = (int)(0.0f * plane[1]);
    vert[1][2] = (int)(0.0f * plane[0]); vert[1][3] = (int)(1.0f * plane[1]);
    vert[2][2] = (int)(1.0f * plane[0]); vert[2][3] = (int)(0.0f * plane[1]);
    vert[3][2] = (int)(1.0f * plane[0]); vert[3][3] = (int)(1.0f * plane[1]);

    // Get aspect corrected sizes within pixel boundaries
    vert[0][0] = (int)(0.0f * ratio_x) + shift_x; vert[0][1] = (int)(0.0f * ratio_y) + shift_y;
    vert[1][0] = (int)(0.0f * ratio_x) + shift_x; vert[1][1] = (int)(1.0f * ratio_y) + shift_y;
    vert[2][0] = (int)(1.0f * ratio_x) + shift_x; vert[2][1] = (int)(0.0f * ratio_y) + shift_y;
    vert[3][0] = (int)(1.0f * ratio_x) + shift_x; vert[3][1] = (int)(1.0f * ratio_y) + shift_y;

    // Get scale, for filtering.
    scale[0] = ratio_x / plane[0];
    scale[1] = ratio_y / plane[1];
}

static
void mat_ortho(float left, float right, float bottom, float top, float Result[4][4])
{
    *(mat4*)Result = (mat4){{[0 ... 15] = 0}};
    Result[0][0] = 2.0f / (right - left);
    Result[1][1] = 2.0f / (top - bottom);
    Result[2][2] = -1.0f;
    Result[3][0] = - (right + left) / (right - left);
    Result[3][1] = - (top + bottom) / (top - bottom);
    Result[3][3] = 1.0f;
}

#define fourcc_code(a, b, c, d) ((__u32)(a) | ((__u32)(b) << 8) | \
				 ((__u32)(c) << 16) | ((__u32)(d) << 24))

int
MALI_InitBlitter(_THIS, MALI_Blitter *blitter, NativeWindowType nw, int rotation)
{
    char *use_hq_scaler;
    GLchar msg[2048] = {}, blit_vert[2048] = {};
    const GLchar *sources[2] = { blit_vert, blit_frag_standard };
    float mat_projection[4][4];
    float vert_buffer_data[4][4];
    float scale[2];

    if ((use_hq_scaler = SDL_getenv("SDL_MALI_HQ_SCALER")) != NULL && *use_hq_scaler == '1') {
        sources[1] = blit_frag_hq;
    } else {
        use_hq_scaler = NULL;
    }

    /* Attempt to initialize necessary functions */
    #define SDL_PROC(ret,func,params) \
        blitter->func = SDL_GL_GetProcAddress(#func); \
        if (!blitter->func) \
        { \
            return 0; \
        }
    #include "SDL_maliblitter_funcs.h"
    #undef SDL_PROC

    blitter->surface = SDL_EGL_CreateSurface(_this, nw);
    if (blitter->surface == EGL_NO_SURFACE) {
        SDL_EGL_SetError("mali-fbdev: Failed to setup blitter EGL Surface", "SDL_EGL_CreateSurface");
        return 0;
    }

    blitter->context = MALI_Blitter_CreateContext(_this, blitter->surface);
    if (blitter->context == EGL_NO_CONTEXT) {
        SDL_EGL_SetError("mali-fbdev: Failed to setup blitter EGL Context", "SDL_EGL_CreateContext");
        return 0;
    }
    
    if (!_this->egl_data->eglMakeCurrent(_this->egl_data->egl_display,
        blitter->surface, 
        blitter->surface, 
        blitter->context))
    {
        SDL_EGL_SetError("mali-fbdev: Unable to make blitter EGL context current", "eglMakeCurrent");
        return 0;
    }

    /* Setup vertex shader coord orientation */
    SDL_snprintf(blit_vert, sizeof(blit_vert), blit_vert_fmt,
        /* rotation */
        (rotation == 0) ? "" :
        (rotation == 1) ? "vTexCoord = vec2(aTexCoord.y, -aTexCoord.x);" :
        (rotation == 2) ? "vTexCoord = vec2(-aTexCoord.x, -aTexCoord.y);" :
        (rotation == 3) ? "vTexCoord = vec2(-aTexCoord.y, aTexCoord.x);" :
        "#error Orientation out of scope",
        /* scalers */
        (use_hq_scaler) ? "vTexCoord = vTexCoord;"
                        : "vTexCoord = vTexCoord / uTexSize;");

    /* Compile vertex shader */
    blitter->vert = blitter->glCreateShader(GL_VERTEX_SHADER);
    blitter->glShaderSource(blitter->vert, 1, &sources[0], NULL);
    blitter->glCompileShader(blitter->vert);
    blitter->glGetShaderInfoLog(blitter->vert, sizeof(msg), NULL, msg);
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "mali-fbdev: Blitter Vertex Shader Info: %s\n", msg);

    /* Compile the fragment shader */
    blitter->frag = blitter->glCreateShader(GL_FRAGMENT_SHADER);
    blitter->glShaderSource(blitter->frag, 1, &sources[1], NULL);
    blitter->glCompileShader(blitter->frag);
    blitter->glGetShaderInfoLog(blitter->frag, sizeof(msg), NULL, msg);
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "mali-fbdev: Blitter Fragment Shader Info: %s\n", msg);

    blitter->prog = blitter->glCreateProgram();
    blitter->glAttachShader(blitter->prog, blitter->vert);
    blitter->glAttachShader(blitter->prog, blitter->frag);

    blitter->glLinkProgram(blitter->prog);
    blitter->loc_aVertCoord = blitter->glGetAttribLocation(blitter->prog, "aVertCoord");
    blitter->loc_aTexCoord = blitter->glGetAttribLocation(blitter->prog, "aTexCoord");
    blitter->loc_uFBOtex = blitter->glGetUniformLocation(blitter->prog, "uFBOTex");
    blitter->loc_uProj = blitter->glGetUniformLocation(blitter->prog, "uProj");
    blitter->loc_uTexSize = blitter->glGetUniformLocation(blitter->prog, "uTexSize");
    blitter->loc_uScale = blitter->glGetUniformLocation(blitter->prog, "uScale");

    blitter->glGetProgramInfoLog(blitter->prog, sizeof(msg), NULL, msg);
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "mali-fbdev: Blitter Program Info: %s\n", msg);

    /* Setup programs */
    blitter->glUseProgram(blitter->prog);
    blitter->glUniform1i(blitter->loc_uFBOtex, 0);

    /* Prepare projection and aspect corrected bounds */
    mat_ortho(0, blitter->viewport_width, 0, blitter->viewport_height, mat_projection);
    get_aspect_correct_coords(
        (int [2]){blitter->viewport_width, blitter->viewport_height},
        (int [2]){blitter->plane_width, blitter->plane_height},
        rotation,
        vert_buffer_data,
        scale
    );

    /* Setup viewport, projection, scale, texture size */
    blitter->glViewport(0, 0, blitter->viewport_width, blitter->viewport_height);
    blitter->glUniformMatrix4fv(blitter->loc_uProj, 1, 0, (GLfloat*)mat_projection);
    blitter->glUniform2f(blitter->loc_uScale, scale[0], scale[1]);
    if (!(rotation & 1))
        blitter->glUniform2f(blitter->loc_uTexSize, blitter->plane_height, blitter->plane_width);
    else
        blitter->glUniform2f(blitter->loc_uTexSize, blitter->plane_width, blitter->plane_height);

    /* Generate buffers */
    blitter->glGenBuffers(1, &blitter->vbo);
    blitter->glGenVertexArraysOES(1, &blitter->vao);

    /* Populate buffers */
    blitter->glBindVertexArrayOES(blitter->vao);
    blitter->glBindBuffer(GL_ARRAY_BUFFER, blitter->vbo);
    blitter->glEnableVertexAttribArray(blitter->loc_aVertCoord);
    blitter->glEnableVertexAttribArray(blitter->loc_aTexCoord);
    blitter->glVertexAttribPointer(blitter->loc_aVertCoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(0 * sizeof(float)));
    blitter->glVertexAttribPointer(blitter->loc_aTexCoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    blitter->glBufferData(GL_ARRAY_BUFFER, sizeof(vert_buffer_data), vert_buffer_data, GL_STATIC_DRAW);

    for (int i = 0; i < 3; i++) {
        EGLint attribute_list[] = {
            EGL_WIDTH, blitter->plane_width,
            EGL_HEIGHT, blitter->plane_height,
            EGL_DMA_BUF_PLANE0_PITCH_EXT, blitter->plane_pitch,
            EGL_LINUX_DRM_FOURCC_EXT, fourcc_code('A', 'R', '2', '4'),
            EGL_DMA_BUF_PLANE0_FD_EXT, blitter->planes[i].fd,
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
            EGL_NONE
        };

        blitter->planes[i].image = _this->egl_data->eglCreateImageKHR(
            _this->egl_data->egl_display,
            EGL_NO_CONTEXT,
            EGL_LINUX_DMA_BUF_EXT,
            (EGLClientBuffer)NULL,
            &attribute_list[0]);
        if (blitter->planes[i].image == EGL_NO_IMAGE_KHR) {
            SDL_EGL_SetError("mali-fbdev: Failed to create Blitter EGL Image", "eglCreateImageKHR");
            return 0;
        }

        blitter->glGenTextures(1, &blitter->planes[i].texture);
        blitter->glActiveTexture(GL_TEXTURE0);
        blitter->glBindTexture(GL_TEXTURE_2D, blitter->planes[i].texture);
        blitter->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        blitter->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        if (use_hq_scaler) {
            // hq scaler requires bilinear filtering to optimize texel fetch count
            blitter->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            blitter->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        } else {
            blitter->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            blitter->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);            
        }
        blitter->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, blitter->planes[i].image);
    }

    return 1;
}

void MALI_Blitter_Blit(_THIS, MALI_Blitter *blitter, int texture)
{
    blitter->glBindVertexArrayOES(blitter->vao);
    blitter->glBindTexture(GL_TEXTURE_2D, blitter->planes[texture].texture);
    blitter->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

int MALI_TripleBufferingThread(void *data)
{
    int first = 1;
    int prevSwapInterval = -1;
    unsigned int page;
    MALI_EGL_Surface *current_surface;
	SDL_WindowData *windowdata;
    SDL_DisplayData *displaydata;
    SDL_VideoDevice* _this;
    MALI_Blitter blitter;
    
    _this = (SDL_VideoDevice*)data;
    windowdata = (SDL_WindowData *)_this->windows->driverdata;
    displaydata = (SDL_DisplayData*)SDL_GetDisplayDriverData(0);

    /* Setup blitter props */
    blitter = (MALI_Blitter){
        .viewport_width = displaydata->vinfo.xres,
        .viewport_height = displaydata->vinfo.yres,
        .plane_width = windowdata->surface[0].pixmap.width,
        .plane_height = windowdata->surface[0].pixmap.height,
        .plane_pitch = windowdata->surface[0].pixmap.planes[0].stride,
        .planes = {
            { .fd = windowdata->surface[0].pixmap.handles[0] },
            { .fd = windowdata->surface[1].pixmap.handles[0] },
            { .fd = windowdata->surface[2].pixmap.handles[0] },
        }
    };

    /* Initialize blitter */
    if (!MALI_InitBlitter(_this, &blitter, (NativeWindowType)&displaydata->native_display, 
        displaydata->rotation))
    {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "mali-fbdev: Failed to create blitter thread context");
        SDL_Quit();
    }

    /* Signal triplebuf available */
	SDL_LockMutex(windowdata->triplebuf_mutex);
	SDL_CondSignal(windowdata->triplebuf_cond);

	for (;;) {
        SDL_CondWait(windowdata->triplebuf_cond, windowdata->triplebuf_mutex);
        if (first) {
            /* 
             * Reset vinfo, otherwise applications can get stuck. This is done
             * a bit late to avoid applications getting rid of the splash screen.
             * Failing to set yres_virtual = 3 causes the display to tear.
             */
            displaydata->vinfo.yoffset = 0;
            displaydata->vinfo.yres_virtual = displaydata->vinfo.yres * 3;
            if (ioctl(displaydata->fb_fd, FBIOPUT_VSCREENINFO, &displaydata->vinfo) < 0) {
                MALI_VideoQuit(_this);
                return SDL_SetError("mali-fbdev: Could not put framebuffer information");
            }

            first = 0;
        }
        
        if (windowdata->triplebuf_thread_stop)
			break;

        if (prevSwapInterval != windowdata->swapInterval) {
            _this->egl_data->eglSwapInterval(_this->egl_data->egl_display, windowdata->swapInterval);
            prevSwapInterval = windowdata->swapInterval;
        }

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
            blitter.glClearColor(0.0, 0.0, 0.0, 1.0);
            blitter.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            MALI_Blitter_Blit(_this, &blitter, windowdata->current_page);
            _this->egl_data->eglSwapBuffers(_this->egl_data->egl_display, blitter.surface);
        }
	}

    /* Execution is done, teardown the allocated resources */ 
    _this->egl_data->eglMakeCurrent(_this->egl_data->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    for (int i = 0; i < 3; i++) {
        blitter.glDeleteTextures(1, &blitter.planes[i].texture);
        _this->egl_data->eglDestroyImageKHR(_this->egl_data->egl_display, blitter.planes[i].image);
    }
    _this->egl_data->eglDestroySurface(_this->egl_data->egl_display, blitter.surface);
    _this->egl_data->eglDestroyContext(_this->egl_data->egl_display, blitter.context);
    _this->egl_data->eglReleaseThread();

    /* Signal thread done */
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

#endif /* SDL_VIDEO_DRIVER_MALI */