#if 1 /*Set it to "1" to enable content*/
#ifndef LV_DRV_CONF_H
#define LV_DRV_CONF_H

/*-----------------
 *  Backend select
 *----------------*/
/* When building with -DUSE_SDL_BACKEND, prefer SDL-based drivers from lv_drivers.
 * Otherwise default to Linux fbdev/evdev. */

#ifdef USE_SDL_BACKEND

/* Disable Linux-specific backends */
#define USE_FBDEV   0
#define USE_EVDEV   0

/* Enable SDL/desktop backends */
#define USE_MONITOR     1   /* Display */
#define USE_MOUSE       1   /* Pointer */
#define USE_KEYBOARD    1   /* Keypad  */
#define USE_MOUSEWHEEL  1   /* Encoder */

/* Optional: override the desktop window size
#  define MONITOR_HOR_RES 800
#  define MONITOR_VER_RES 480
#  define MONITOR_DPI     140
*/

#else /* !USE_SDL_BACKEND -> default Linux fbdev/evdev */

/*-----------------
 *  Display driver
 *----------------*/

/* Linux framebuffer device (fbdev) */
#define USE_FBDEV   1
#if USE_FBDEV
#  define FBDEV_PATH "/dev/fb0"
#endif

/*-----------------
 *  Input devices
 *----------------*/

/* Linux evdev input (mouse/touch) */
#define USE_EVDEV  1
#if USE_EVDEV
#  define EVDEV_NAME "/dev/input/event0"
/* If you prefer detecting a mouse-like device:
#  define EVDEV_NAME "/dev/input/mice" */
#  define EVDEV_SWAP_AXES         0
#  define EVDEV_CALIBRATE         0
#endif

/* Ensure desktop backends are disabled in Linux mode */
#define USE_MONITOR     0
#define USE_MOUSE       0
#define USE_KEYBOARD    0
#define USE_MOUSEWHEEL  0

#endif /* USE_SDL_BACKEND */

#endif /* LV_DRV_CONF_H */
#endif /*End of "Content enable"*/
