#if 1 /*Set it to "1" to enable content*/
#ifndef LV_CONF_H
#define LV_CONF_H

/*====================
   Graphical settings
 *====================*/

/* Maximal horizontal and vertical resolution to support by the library. */
#define LV_HOR_RES_MAX          (480)
#define LV_VER_RES_MAX          (320)

/* Color settings */
#define LV_COLOR_DEPTH          32
#define LV_COLOR_16_SWAP        0

/* Enable anti-aliasing (1: Enable, 0: Disable) */
#define LV_ANTIALIAS            1

/* Tick configuration
 * If LV_TICK_CUSTOM is 0 (default) you must call lv_tick_inc(ms) periodically from your system tick.
 * If you set LV_TICK_CUSTOM to 1 you must provide a function: uint32_t lv_tick_get(void) that returns ms. */
#define LV_TICK_CUSTOM          0

/*====================
   Feature usage
 *====================*/

/* Enable the log module */
#define LV_USE_LOG              1
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF           1

/* GPU (useful on some MCUs; typically disabled on PC/Linux builds) */
#define LV_USE_GPU              0

/*====================
   Fonts
 *====================*/

/* Enable only the fonts you actively use to keep the binary small. */
#define LV_FONT_MONTSERRAT_12   0
#define LV_FONT_MONTSERRAT_14   0
#define LV_FONT_MONTSERRAT_16   0
#define LV_FONT_MONTSERRAT_18   1 /* Used in main.c */
#define LV_FONT_MONTSERRAT_20   0
#define LV_FONT_MONTSERRAT_22   0
#define LV_FONT_MONTSERRAT_24   0
#define LV_FONT_MONTSERRAT_26   0
#define LV_FONT_MONTSERRAT_28   0
#define LV_FONT_MONTSERRAT_30   0
#define LV_FONT_MONTSERRAT_32   0

/*====================
   Others
 *====================*/

/* Use the built-in themes (optional) */
#define LV_USE_THEME_MATERIAL   1

/* Use the built-in file system API stubs (not required here) */
#define LV_USE_FS_STDIO         0

#endif /*LV_CONF_H*/
#endif /*End of "Content enable"*/
