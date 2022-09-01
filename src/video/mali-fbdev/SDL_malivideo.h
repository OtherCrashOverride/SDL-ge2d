#ifndef _SDL_malivideo_h
#define _SDL_malivideo_h

#include "../../SDL_internal.h"
#include "../SDL_sysvideo.h"

#include "SDL_egl.h"

#include <EGL/egl.h>
#include <linux/vt.h>
#include <linux/fb.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include "mali.h"
#include "ion.h"
#include "ge2d.h"
#include "ge2d_cmd.h"

typedef struct SDL_DisplayData
{
    struct fbdev_window native_display;
    NativePixmapType (*egl_create_pixmap_ID_mapping)(mali_pixmap *);
    NativePixmapType (*egl_destroy_pixmap_ID_mapping)(int id);

    int ge2d_fd, ion_fd;
} SDL_DisplayData;

typedef struct SDL_WindowData
{
    // A pixmap is backed by multiple ION allocated backbuffers
    mali_pixmap pixmap;
    NativePixmapType pixmap_handle;

    struct {
        int shared_fd;
        int handle;
    } ion_surface[3];

    // The created EGL Surface is backed by a mali pixmap
    EGLSurface egl_surface;
} SDL_WindowData;

/****************************************************************************/
/* SDL_VideoDevice functions declaration                                    */
/****************************************************************************/

/* Display and window functions */
int MALI_VideoInit(_THIS);
void MALI_VideoQuit(_THIS);
void MALI_GetDisplayModes(_THIS, SDL_VideoDisplay * display);
int MALI_SetDisplayMode(_THIS, SDL_VideoDisplay * display, SDL_DisplayMode * mode);
int MALI_CreateWindow(_THIS, SDL_Window * window);
void MALI_SetWindowTitle(_THIS, SDL_Window * window);
void MALI_SetWindowPosition(_THIS, SDL_Window * window);
void MALI_SetWindowSize(_THIS, SDL_Window * window);
void MALI_ShowWindow(_THIS, SDL_Window * window);
void MALI_HideWindow(_THIS, SDL_Window * window);
void MALI_DestroyWindow(_THIS, SDL_Window * window);

/* Window manager function */
SDL_bool MALI_GetWindowWMInfo(_THIS, SDL_Window * window,
                             struct SDL_SysWMinfo *info);

/* Event functions */
void MALI_PumpEvents(_THIS);

#endif /* _SDL_malivideo_h */
