
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

#define DISP_BUF_SIZE (320 * 240)  /* Rendering buffer in pixels; may be smaller than the display resolution */

#ifndef UI_UPDATE_INTERVAL_MS
#define UI_UPDATE_INTERVAL_MS 1000  /* UI refresh timer interval in milliseconds */
#endif

#ifndef API_REFRESH_SEC
#define API_REFRESH_SEC 30          /* Background API refresh interval in seconds */
#endif

#ifndef DISP_HOR_RES
#define DISP_HOR_RES 480            /* Default horizontal resolution for display driver */
#endif

#ifndef DISP_VER_RES
#define DISP_VER_RES 320            /* Default vertical resolution for display driver */
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

// Function prototypes
void hal_init(void);
void update_display_data(void);
void *api_update_thread(void *arg);
float get_temperature_from_api(void);
float get_pressure_from_api(void);
float get_humidity_from_api(void);
void get_current_time(char *time_str, size_t size);
void *lvgl_tick_thread(void *arg);
void update_display_timer_cb(lv_timer_t *timer);

int main(void)
{
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
    while(1) {
        // Fetch data from APIs (placeholder implementations below)
        float t = get_temperature_from_api();
        float p = get_pressure_from_api();
        float h = get_humidity_from_api();
        char ts[sizeof current_data.time_str];
        get_current_time(ts, sizeof(ts));

        // Commit new snapshot atomically for the GUI thread to consume
        pthread_mutex_lock(&current_data_mutex);
        current_data.temperature = t;
        current_data.pressure = p;
        current_data.humidity = h;
        strncpy(current_data.time_str, ts, sizeof(current_data.time_str));
        current_data.time_str[sizeof(current_data.time_str) - 1] = '\0';
        pthread_mutex_unlock(&current_data_mutex);

        // Wait 30 seconds before next update
        sleep(API_REFRESH_SEC);
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
    pthread_mutex_lock(&current_data_mutex);
    t = current_data.temperature;
    p = current_data.pressure;
    h = current_data.humidity;
    strncpy(time_str, current_data.time_str, sizeof(time_str));
    time_str[sizeof(time_str) - 1] = '\0';
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

// API placeholder functions - implement these to connect to actual weather APIs
/**
 * get_temperature_from_api
 * Placeholder that simulates a temperature reading from a weather API.
 * return: temperature in Celsius
 */
float get_temperature_from_api(void)
{
    // TODO: Implement actual API call to weather service (e.g., libcurl + JSON)
    printf("Fetching temperature from API...\n");
    
    // Simulate API response
    return 22.5f; // Dummy temperature in Celsius
}

/**
 * get_pressure_from_api
 * Placeholder that simulates an atmospheric pressure reading from a weather API.
 * return: pressure in hPa
 */
float get_pressure_from_api(void)
{
    // TODO: Implement actual API call to weather service
    printf("Fetching pressure from API...\n");
    
    // Simulate API response
    return 1013.2f; // Dummy pressure in hPa
}

/**
 * get_humidity_from_api
 * Placeholder that simulates a relative humidity reading from a weather API.
 * return: humidity percent (0..100)
 */
float get_humidity_from_api(void)
{
    // TODO: Implement actual API call to weather service
    printf("Fetching humidity from API...\n");
    
    // Simulate API response
    return 65.0f; // Dummy humidity percentage
}

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
