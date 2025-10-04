
/*
 * LVGL Weather Station Demo
 *
 * Overview
 * --------
 * A minimal weather dashboard built with LVGL running on the Linux framebuffer
 * (fbdev) with evdev input. It renders four widgets (temperature, pressure,
 * humidity, time) and periodically refreshes their data using a background
 * thread that simulates weather API calls.
 *
 * Key Components
 * --------------
 * - LVGL core (lv_init, lv_timer_handler, lv_tick_inc)
 * - Linux framebuffer display driver (fbdev)
 * - Linux input driver (evdev) for mouse/touch
 * - POSIX threads: one for LVGL ticks, one for data refresh
 *
 * Threading Model
 * ---------------
 * - Main thread: initializes LVGL/HAL, builds the UI, and runs lv_timer_handler
 *   in a simple loop.
 * - Tick thread: calls lv_tick_inc(1) every 1 ms. LVGL uses this for time-based
 *   tasks, animations, and timers.
 * - API update thread: fetches data every 30 seconds and requests UI updates.
 *
 * IMPORTANT: LVGL API calls are not thread-safe. All LVGL object access and
 * updates should occur from the same thread that runs lv_timer_handler (the
 * "GUI thread"). The current example directly updates labels from the API
 * thread for simplicity; in production, prefer one of the following:
 *   - Use lv_timer to schedule periodic updates on the GUI thread.
 *   - Use lv_async_call or a message/queue to marshal data to the GUI thread.
 *   - Protect LVGL with a global mutex and ensure consistent ownership.
 *
 * Display Buffering
 * -----------------
 * DISP_BUF_SIZE is set to 320x240. LVGL supports rendering in smaller chunks
 * than the full display resolution, so the buffer can be smaller than the
 * window/screen size (e.g., 480x320). Increase buffer size or use double
 * buffering for better performance on larger displays.
 *
 * Placeholders and Extensibility
 * ------------------------------
 * The get_*_from_api functions return dummy values. Replace them with real
 * network calls (e.g., libcurl, sockets) and robust error handling. Consider
 * using NaN or dedicated validity flags to represent missing data instead of
 * overloading zero values.
 *
 * Portability Notes
 * -----------------
 * fbdev/evdev target Linux. On non-Linux hosts, use the appropriate LVGL port
 * (SDL, Wayland, etc.). Time formatting uses localtime; for thread-safety on
 * POSIX, localtime_r is recommended.
 */
#include "lvgl/lvgl.h"

/* Backend selection: fbdev/evdev (default) or SDL (desktop) via USE_SDL_BACKEND */
#ifdef USE_SDL_BACKEND
#include "lv_drivers/display/monitor.h"
#include "lv_drivers/indev/mouse.h"
#include "lv_drivers/indev/keyboard.h"
#include "lv_drivers/indev/mousewheel.h"
#else
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"
#endif
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#ifdef __linux__
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#endif

#if defined(__linux__) && !defined(DISABLE_BME280)
#define BME_FEATURE 1
#else
#define BME_FEATURE 0
#endif

#define DISP_BUF_SIZE (320 * 240)  /* Rendering buffer in pixels; may be smaller than the display resolution */

#ifndef UI_UPDATE_INTERVAL_MS
#define UI_UPDATE_INTERVAL_MS 1000  /* UI refresh timer interval in milliseconds */
#endif

/* Deprecated: API_REFRESH_SEC retained for backward compatibility; prefer SENSOR_REFRESH_SEC */
#ifndef API_REFRESH_SEC
#define API_REFRESH_SEC 30          /* Background API refresh interval in seconds */
#endif
#ifdef API_REFRESH_SEC
#warning "API_REFRESH_SEC is deprecated; use SENSOR_REFRESH_SEC instead"
#endif

#ifndef SENSOR_REFRESH_SEC
#ifdef API_REFRESH_SEC
#define SENSOR_REFRESH_SEC API_REFRESH_SEC
#else
#define SENSOR_REFRESH_SEC 30          /* Background sensor refresh interval in seconds */
#endif
#endif

#ifndef DISP_HOR_RES
#define DISP_HOR_RES 480            /* Default horizontal resolution for display driver */
#endif

#ifndef DISP_VER_RES
#define DISP_VER_RES 320            /* Default vertical resolution for display driver */
#endif

/* Default Linux I2C device path for BME280 (ensure visible before use) */
#ifndef BME280_I2C_DEV
#define BME280_I2C_DEV "/dev/i2c-1"
#endif

#if BME_FEATURE
#include "BME280.h"
#include "I2CDevice.h"
#endif

/* Color constants (use hex to avoid palette dependency) */
#define COLOR_TEMP     lv_color_hex(0xF44336)  /* Red 500 */
#define COLOR_PRESSURE lv_color_hex(0x2196F3)  /* Blue 500 */
#define COLOR_HUMIDITY lv_color_hex(0x4CAF50)  /* Green 500 */
#define COLOR_TIME     lv_color_hex(0x9C27B0)  /* Purple 500 */

// UI widget handles created during UI construction; used to update label text.
static lv_obj_t *temp_label;
static lv_obj_t *pressure_label;
static lv_obj_t *humidity_label;
static lv_obj_t *time_label;
static lv_obj_t *g_source_label_ref = NULL; // track source of data

#if BME_FEATURE
static unsigned long g_bme_ok = 0;
static unsigned long g_bme_err = 0;
#endif

/**
 * weather_data_t
 * In-memory representation of the current weather snapshot shown on screen.
 *
 * Fields use SI units:
 * - temperature: Celsius
 * - pressure: hectopascals (hPa)
 * - humidity: percentage [0..100]
 * - time_str: formatted as HH:MM (24h)
 *
 * Note: This demo uses NaN (math.h) to represent "unknown" values when
 * formatting output. In production, consider explicit validity flags for
 * clarity and portability.
 */
typedef struct {
    float temperature;
    float pressure;
    float humidity;
    char time_str[32];
} weather_data_t;

static weather_data_t current_data = { .temperature = NAN, .pressure = NAN, .humidity = NAN, .time_str = "" };
static pthread_mutex_t current_data_mutex = PTHREAD_MUTEX_INITIALIZER;  // Protects access to current_data
static int g_last_source_is_bme = 0;  // 1 if last update used BME, else 0 (no sensor data)
// Function prototypes
void hal_init(void);
void update_display_data(void);
void *api_update_thread(void *arg);
void get_current_time(char *time_str, size_t size);
void *lvgl_tick_thread(void *arg);
void update_display_timer_cb(lv_timer_t *timer);
#ifdef __linux__
static int bme280_init_linux(const char *i2c_path);
static int bme280_read_raw(float *temp_c, float *press_hpa, float *humid_rh);
static int bme_is_inited(void);
static uint8_t bme_get_addr(void);
#endif
#if BME_FEATURE
static const char *g_cli_i2c_path = NULL; // optional CLI override
#endif

int main(int argc, char **argv)
{
#if BME_FEATURE
    // Parse command-line for --i2c <path> or --i2c=<path>
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--i2c=", 7) == 0) {
            g_cli_i2c_path = argv[i] + 7;
        } else if (strcmp(argv[i], "--i2c") == 0 && i + 1 < argc) {
            g_cli_i2c_path = argv[i + 1];
            ++i;
        }
    }
#endif
    // Initialize LVGL core
    lv_init();

    // Initialize hardware abstraction layer (display + input drivers)
    hal_init();

    // Start LVGL tick thread (1 ms tick)
    pthread_t tick_thread;
    pthread_create(&tick_thread, NULL, lvgl_tick_thread, NULL);

    // Create main window (LVGL v8 API)
    lv_disp_t *disp = lv_disp_get_default();
    lv_coord_t disp_w = lv_disp_get_hor_res(disp);
    lv_coord_t disp_h = lv_disp_get_ver_res(disp);

    lv_obj_t *win = lv_win_create(lv_scr_act(), 40);
    lv_win_add_title(win, "Weather Station");
    lv_obj_set_size(win, disp_w, disp_h);
    lv_obj_center(win);

    // Get window content area and add padding
    lv_obj_t *win_content = lv_win_get_content(win);
    lv_obj_set_style_pad_all(win_content, 20, LV_PART_MAIN);

    // Use flex layout for responsive positioning
    lv_obj_set_flex_flow(win_content, LV_FLEX_FLOW_ROW_WRAP);
    const int gap = 12;             // inter-item spacing used by flex
    const int content_pad = 20;     // must match pad_all above
    lv_obj_set_style_pad_row(win_content, gap, LV_PART_MAIN);
    lv_obj_set_style_pad_column(win_content, gap, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(win_content, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(win_content, 20, LV_PART_MAIN);

    // Compute a reasonable base card size from the display resolution
    int base_card_w = (int)((disp_w - 2*content_pad - gap) / 2); // target 2 columns
    if (base_card_w < 140) base_card_w = 140;                    // enforce a minimum width
    int base_card_h = disp_h / 6;                                // proportional height
    if (base_card_h < 60) base_card_h = 60;                      // enforce a minimum height

    // Create temperature widget
    lv_obj_t *temp_container = lv_obj_create(win_content);
    lv_obj_set_size(temp_container, base_card_w, base_card_h);
    lv_obj_set_style_bg_color(temp_container, COLOR_TEMP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(temp_container, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_radius(temp_container, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(temp_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(temp_container, 6, LV_PART_MAIN);
    lv_obj_set_flex_grow(temp_container, 1);

    lv_obj_t *temp_title = lv_label_create(temp_container);
    lv_label_set_text(temp_title, "Temperature");
    lv_obj_align(temp_title, LV_ALIGN_TOP_MID, 0, 0);

    temp_label = lv_label_create(temp_container);
    lv_label_set_text(temp_label, "-- °C");
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_align(temp_label, LV_ALIGN_BOTTOM_MID, 0, 0);

    // Create pressure widget
    lv_obj_t *pressure_container = lv_obj_create(win_content);
    lv_obj_set_size(pressure_container, base_card_w, base_card_h);
    lv_obj_set_style_bg_color(pressure_container, COLOR_PRESSURE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pressure_container, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_radius(pressure_container, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(pressure_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(pressure_container, 6, LV_PART_MAIN);
    lv_obj_set_flex_grow(pressure_container, 1);

    lv_obj_t *pressure_title = lv_label_create(pressure_container);
    lv_label_set_text(pressure_title, "Pressure");
    lv_obj_align(pressure_title, LV_ALIGN_TOP_MID, 0, 0);

    pressure_label = lv_label_create(pressure_container);
    lv_label_set_text(pressure_label, "-- hPa");
    lv_obj_set_style_text_font(pressure_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_align(pressure_label, LV_ALIGN_BOTTOM_MID, 0, 0);

    // Create humidity widget
    lv_obj_t *humidity_container = lv_obj_create(win_content);
    lv_obj_set_size(humidity_container, base_card_w, base_card_h);
    lv_obj_set_style_bg_color(humidity_container, COLOR_HUMIDITY, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(humidity_container, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_radius(humidity_container, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(humidity_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(humidity_container, 6, LV_PART_MAIN);
    lv_obj_set_flex_grow(humidity_container, 1);

    lv_obj_t *humidity_title = lv_label_create(humidity_container);
    lv_label_set_text(humidity_title, "Humidity");
    lv_obj_align(humidity_title, LV_ALIGN_TOP_MID, 0, 0);

    humidity_label = lv_label_create(humidity_container);
    lv_label_set_text(humidity_label, "-- %");
    lv_obj_set_style_text_font(humidity_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_align(humidity_label, LV_ALIGN_BOTTOM_MID, 0, 0);

    // Create time widget
    lv_obj_t *time_container = lv_obj_create(win_content);
    lv_obj_set_size(time_container, base_card_w, base_card_h);
    lv_obj_set_style_bg_color(time_container, COLOR_TIME, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(time_container, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_radius(time_container, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(time_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(time_container, 6, LV_PART_MAIN);
    lv_obj_set_flex_grow(time_container, 1);

    lv_obj_t *time_title = lv_label_create(time_container);
    lv_label_set_text(time_title, "Time");
    lv_obj_align(time_title, LV_ALIGN_TOP_MID, 0, 0);

    time_label = lv_label_create(time_container);
    lv_label_set_text(time_label, "-- : --");
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_align(time_label, LV_ALIGN_BOTTOM_MID, 0, 0);

    // Add a source/status label at bottom of the window
    lv_obj_t *src_label = lv_label_create(win_content);
    lv_label_set_text(src_label, "Source: --");
    lv_obj_set_style_text_font(src_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(src_label, lv_color_hex(0x607D8B), LV_PART_MAIN); // blue-grey
    lv_obj_align(src_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    // Keep a handle for updates
    g_source_label_ref = src_label;

    // Start API update thread (periodic data refresh)
    pthread_t api_thread;
    pthread_create(&api_thread, NULL, api_update_thread, NULL);

    // Create a GUI-thread timer to periodically update labels from current_data
    // This ensures LVGL API calls occur on the GUI thread.
    lv_timer_create(update_display_timer_cb, UI_UPDATE_INTERVAL_MS, NULL); // update every 1s

    // Main loop: process LVGL timers and let the CPU rest briefly
    while(1) {
        lv_timer_handler();
        usleep(5000);
    }

    return 0;
}

/**
 * hal_init
 * Initialize the LVGL hardware abstraction layer:
 * - fbdev display driver with a single color buffer of size DISP_BUF_SIZE
 * - evdev input driver configured as a pointer device (mouse/touch)
 *
 * Call this once from the main thread before creating LVGL objects.
 */
void hal_init(void)
{
#ifdef USE_SDL_BACKEND
    /* Initialize SDL-based display (monitor) */
    monitor_init();

    /* Create a display draw buffer (LVGL v8 API) */
    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t buf1[DISP_BUF_SIZE];
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, DISP_BUF_SIZE);

    /* Create and register display driver */
    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.flush_cb = monitor_flush;   /* SDL/monitor flush */
    disp_drv.draw_buf = &draw_buf;
    disp_drv.hor_res = DISP_HOR_RES;     /* or rely on MONITOR_* from lv_drv_conf.h */
    disp_drv.vert_res = DISP_VER_RES;
    lv_disp_drv_register(&disp_drv);

    /* Initialize SDL-based input devices */
    mouse_init();
    keyboard_init();
    mousewheel_init();

    /* Register mouse as a pointer device */
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = mouse_read;
    lv_indev_drv_register(&indev_drv);

    /* Register keyboard as a keypad device (optional) */
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_KEYPAD;
    indev_drv.read_cb = keyboard_read;
    lv_indev_drv_register(&indev_drv);

    /* Register mouse wheel as an encoder device (optional) */
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_ENCODER;
    indev_drv.read_cb = mousewheel_read;
    lv_indev_drv_register(&indev_drv);
#else
    // Initialize display driver (Linux framebuffer)
    fbdev_init();

    // Create a display draw buffer (LVGL v8 API)
    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t buf1[DISP_BUF_SIZE];
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, DISP_BUF_SIZE);

    // Create and register display driver
    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.flush_cb = fbdev_flush;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.hor_res = DISP_HOR_RES;
    disp_drv.vert_res = DISP_VER_RES;
    lv_disp_drv_register(&disp_drv);

    // Initialize input driver (mouse/touch via evdev)
    evdev_init();
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = evdev_read;
    lv_indev_drv_register(&indev_drv);
#endif
}

/**
 * lvgl_tick_thread
 * POSIX thread entry point that increments the LVGL tick counter every 1 ms.
 *
 * arg: unused
 * returns: NULL
 */
void *lvgl_tick_thread(void *arg)
{
    while(1) {
        lv_tick_inc(1);  // LVGL tick every 1ms
        usleep(1000);    // Sleep for 1ms
    }
    return NULL;
}

/**
 * api_update_thread
 * POSIX thread entry point that periodically fetches weather data and requests
 * a UI update.
 *
 * WARNING: LVGL is not thread-safe. This demo calls lv_label_set_text directly
 * from this thread via update_display_data for brevity. Prefer handing off data
 * to the GUI thread via lv_timer, lv_async_call, or a thread-safe queue.
 *
 * arg: unused
 * returns: NULL
 */
void *api_update_thread(void *arg)
{
    // Allow override of I2C path via env or argv (argv not available here; we use env only)
#if BME_FEATURE
    const char *env_path = getenv("BME280_I2C_DEV");
    const char *i2c_path = g_cli_i2c_path ? g_cli_i2c_path : (env_path && env_path[0] ? env_path : BME280_I2C_DEV);
#endif
    while(1) {
#if BME_FEATURE
        static int bme_tried = 0;
        if (!bme_tried) {
            if (bme280_init_linux(i2c_path) == 0) {
                printf("BME280 initialized over I2C at %s (addr 0x%02X)\n", i2c_path, (unsigned)bme_get_addr());
            } else {
                printf("BME280 not found on %s; sensor data unavailable.\n", i2c_path);
            }
            bme_tried = 1;
        }
#endif
        float t = NAN, p = NAN, h = NAN;
        int inc_ok = 0, inc_err = 0;
        int last_is_bme = 0;
#if BME_FEATURE
        if (bme_is_inited()) {
            if (bme280_read_raw(&t, &p, &h) != 0) {
                // if a read fails, keep values as NAN
                t = NAN; p = NAN; h = NAN;
                inc_err = 1;
                last_is_bme = 0;
             } else {
                inc_ok = 1;
                last_is_bme = 1;
             }
        } else {
            last_is_bme = 0;
        }
#else
        // No BME feature compiled in
        last_is_bme = 0;
#endif
        char ts[sizeof current_data.time_str];
        get_current_time(ts, sizeof(ts));

        pthread_mutex_lock(&current_data_mutex);
        current_data.temperature = t;
        current_data.pressure = p;
        current_data.humidity = h;
        strncpy(current_data.time_str, ts, sizeof(current_data.time_str));
        current_data.time_str[sizeof(current_data.time_str) - 1] = '\0';
        g_last_source_is_bme = last_is_bme;
#if BME_FEATURE
        if (inc_ok) g_bme_ok++;
        if (inc_err) g_bme_err++;
#endif
        pthread_mutex_unlock(&current_data_mutex);

        sleep(SENSOR_REFRESH_SEC);
    }
    return NULL;
}

/**
 * update_display_data
 * Format the current_data fields and set the corresponding LVGL labels.
 *
 * Formatting Rules
 * - If a value is NaN, show "--" for that metric. Consider using NaN to
 *   differentiate an actual zero-valued reading from "unknown".
 *
 * Thread-safety
 * - Should be called from the GUI thread in production. See the top-of-file
 *   notes for approaches to marshal updates safely.
 */
void update_display_data(void)
{
    char temp_str[32];
    char pressure_str[32];
    char humidity_str[32];
    char time_str[32];

    // Take a thread-safe snapshot of the latest data
    float t, p, h;
    int last_is_bme_snapshot = 0;
    unsigned long bme_ok_snapshot = 0;
    unsigned long bme_err_snapshot = 0;
    pthread_mutex_lock(&current_data_mutex);
    t = current_data.temperature;
    p = current_data.pressure;
    h = current_data.humidity;
    strncpy(time_str, current_data.time_str, sizeof(time_str));
    time_str[sizeof(time_str) - 1] = '\0';
    last_is_bme_snapshot = g_last_source_is_bme;
#if BME_FEATURE
    bme_ok_snapshot = g_bme_ok;
    bme_err_snapshot = g_bme_err;
#endif
    pthread_mutex_unlock(&current_data_mutex);

    // Format temperature
    if (!isnan(t)) {
        snprintf(temp_str, sizeof(temp_str), "%.1f °C", t);
    } else {
        strcpy(temp_str, "-- °C");
    }

    // Format pressure
    if (!isnan(p)) {
        snprintf(pressure_str, sizeof(pressure_str), "%.0f hPa", p);
    } else {
        strcpy(pressure_str, "-- hPa");
    }

    // Format humidity
    if (!isnan(h)) {
        snprintf(humidity_str, sizeof(humidity_str), "%.1f %%", h);
    } else {
        strcpy(humidity_str, "-- %");
    }

    // Format time (if available)
    const char *time_to_show = (time_str[0] != '\0') ? time_str : "--:--";

    // Update labels. NOTE: LVGL APIs are not thread-safe; this is called on GUI thread via lv_timer.
    lv_label_set_text(temp_label, temp_str);
    lv_label_set_text(pressure_label, pressure_str);
    lv_label_set_text(humidity_label, humidity_str);
    lv_label_set_text(time_label, time_to_show);
    
    // Update source label
    if (g_source_label_ref) {
#if BME_FEATURE
        if (bme_is_inited() && last_is_bme_snapshot) {
            char srcbuf[64];
            snprintf(srcbuf, sizeof(srcbuf), "Source: BME280 (ok=%lu err=%lu)", bme_ok_snapshot, bme_err_snapshot);
            lv_label_set_text(g_source_label_ref, srcbuf);
        } else if (bme_is_inited()) {
            char srcbuf[64];
            snprintf(srcbuf, sizeof(srcbuf), "Source: BME280 (read error, err=%lu)", bme_err_snapshot);
            lv_label_set_text(g_source_label_ref, srcbuf);
        } else {
            lv_label_set_text(g_source_label_ref, "Source: BME280 unavailable");
        }
#else
        lv_label_set_text(g_source_label_ref, "Source: BME280 disabled at build time");
#endif
    }
}

/**
 * update_display_timer_cb
 * LVGL timer callback that runs on the GUI thread to refresh label text.
 */
void update_display_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_display_data();
}

// Dummy API functions removed: data now only comes from BME280 (if available) and system clock

/**
 * get_current_time
 * Format the current local time as HH:MM (24-hour clock).
 *
 * time_str: destination buffer where the formatted time is written
 * size:     size of the destination buffer
 *
 * Note: localtime() is not thread-safe. For multi-threaded usage prefer
 * localtime_r() where available.
 */
void get_current_time(char *time_str, size_t size)
{
    time_t rawtime;
    struct tm timeinfo;

    time(&rawtime);
    localtime_r(&rawtime, &timeinfo);
    strftime(time_str, size, "%H:%M", &timeinfo);
}

/* Minimal BME280 state and helpers (Linux I2C). Falls back to dummy API if unavailable. */
#ifdef __linux__
#if BME_FEATURE
static I2CDevice g_i2c_dev = { .fd = -1 };
static bme280_t g_bme_dev;
static int bme_inited = 0;
static uint8_t bme_addr = 0;
/* g_bme_ok/g_bme_err are defined at the top-level (one definition only) */

static int bme_bus_read(void *user, uint8_t reg, uint8_t *buf, size_t len) {
    I2CDevice *dev = (I2CDevice *)user;
    return i2c_device_read_reg(dev, reg, buf, len, 1) == 0 ? 0 : BME280_E_COMM;
}

static int bme_bus_write(void *user, uint8_t reg, const uint8_t *buf, size_t len) {
    I2CDevice *dev = (I2CDevice *)user;
    return i2c_device_write_reg(dev, reg, buf, len, 1) == 0 ? 0 : BME280_E_COMM;
}

static void bme_bus_delay(void *user, uint32_t ms) {
    (void)user;
    usleep(ms * 1000);
}

static int bme280_init_try_addr(const char *i2c_path, uint8_t addr) {
    if (i2c_device_open(&g_i2c_dev, i2c_path, addr) != 0) return -1;
    bme280_bus_t bus = {
        .read = bme_bus_read,
        .write = bme_bus_write,
        .delay_ms = bme_bus_delay,
        .user = &g_i2c_dev
    };
    int rc = bme280_init(&g_bme_dev, &bus, addr);
    if (rc != BME280_OK) {
        i2c_device_close(&g_i2c_dev);
        return -1;
    }
    /* Configure a simple continuous measurement */
    bme280_set_oversampling(&g_bme_dev, BME280_OSRS_X1, BME280_OSRS_X1, BME280_OSRS_X1);
    bme280_set_filter(&g_bme_dev, BME280_FILTER_OFF);
    bme280_set_standby(&g_bme_dev, BME280_STANDBY_1000_MS);
    bme280_set_mode(&g_bme_dev, BME280_NORMAL_MODE);
    pthread_mutex_lock(&current_data_mutex);
    bme_addr = addr;
    bme_inited = 1;
    pthread_mutex_unlock(&current_data_mutex);
    return 0;
}

static int bme280_init_linux(const char *i2c_path) {
    if (bme_is_inited()) return 0;
    const uint8_t addrs[] = { BME280_I2C_ADDR_SDO_HIGH, BME280_I2C_ADDR_SDO_LOW };
    for (size_t i = 0; i < sizeof(addrs)/sizeof(addrs[0]); ++i) {
        if (bme280_init_try_addr(i2c_path, addrs[i]) == 0) return 0;
    }
    return -1;
}

static int bme280_read_raw(float *temp_c, float *press_hpa, float *humid_rh) {
    if (!bme_is_inited()) return -1;
    bme280_reading_t r;
    int rc = bme280_read_measurement(&g_bme_dev, &r);
    if (rc != BME280_OK) return -1;
    if (temp_c) *temp_c = r.temperature_c;
    if (press_hpa) *press_hpa = r.pressure_pa / 100.0f; /* Pa -> hPa */
    if (humid_rh) *humid_rh = r.humidity_rh;
    return 0;
}

static int bme_is_inited(void) {
    int v;
    pthread_mutex_lock(&current_data_mutex);
    v = bme_inited;
    pthread_mutex_unlock(&current_data_mutex);
    return v;
}
static uint8_t bme_get_addr(void) {
    uint8_t a;
    pthread_mutex_lock(&current_data_mutex);
    a = bme_addr;
    pthread_mutex_unlock(&current_data_mutex);
    return a;
}
#endif /* BME_FEATURE */
#endif /* __linux__ */


/* Remove legacy BME280 low-level implementation and duplicate defines */
// (Old ad-hoc I2C helpers and BME280_REG_* definitions replaced by BME280 driver)
