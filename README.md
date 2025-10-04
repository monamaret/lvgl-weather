
# LVGL Weather Station Demo

## Overview
- A minimal weather dashboard built with LVGL (v8) targeting the Linux framebuffer (fbdev) with evdev input by default, with an optional SDL desktop backend. It renders four widgets (temperature, pressure, humidity, time) plus a data source indicator. A background thread fetches data (BME280 on Linux or simulated API), while a GUI-thread LVGL timer updates labels so all LVGL calls stay on the GUI thread.
- Refactored to use portable, embedded-friendly C libraries for sensors and I/O. On Raspberry Pi (Raspberry Pi OS/Debian) the app can read a BME280 over I2C; otherwise it falls back to simulated API values.

## Repository layout
- main.c: Application entry point, UI creation, threads, and HAL init
- Makefile: Build, clean, Linux dependency install, and LVGL/lv_drivers setup
- lv_conf.h: LVGL configuration (fonts, logging, resolution, etc.)
- lv_drv_conf.h: LVGL driver configuration (fbdev/evdev paths, etc.)
- BME280.c / BME280.h: Portable BME280 sensor driver (no Arduino required)
- I2CDevice.h: Minimal I2C helper for Linux /dev/i2c-* access
- SPIDevice.h (future use), Sensor.h, BME280_I2CDevice.* and BME280_SPIDevice.*: portable bus abstractions for sensors

## Build and run: Raspberry Pi (Raspberry Pi OS Bookworm)

1) Install prerequisites
   sudo apt-get update
   sudo apt-get install -y build-essential git
   # Optional but useful for I2C debugging on Pi 4B
   sudo apt-get install -y i2c-tools

   Notes
   - fbdev and evdev are kernel/driver interfaces; they don't require SDL on a headless target. If you plan to switch to the SDL desktop backend (for running under the Pi desktop), also install: sudo apt-get install -y libsdl2-dev pkg-config
   - You can build the desktop/SDL variant without editing code by adding the flag USE_SDL_BACKEND=1 to make. Example:
     make clean && make USE_SDL_BACKEND=1

2) Enable I2C (for BME280 sensor)
   - Using raspi-config (interactive):
     sudo raspi-config
     • Interface Options -> I2C -> Enable
   - Or non-interactive:
     sudo raspi-config nonint do_i2c 0
   - Reboot if prompted: sudo reboot

   Wiring (BME280 -> Raspberry Pi 4B 40-pin header):
   - VCC -> 3V3 (Pin 1 or 17)
   - GND -> GND (Pin 6, 9, 14, 20, 25, 30, 34, 39)
   - SDA -> GPIO2 SDA1 (Pin 3)
   - SCL -> GPIO3 SCL1 (Pin 5)
   Typical I2C address is 0x76 or 0x77.

   Verify the sensor is detected (after enabling I2C):
   i2cdetect -y 1
   # Look for 0x76 or 0x77 in the matrix

3) (Optional but recommended for fbdev) Boot to console
   - fbdev works best without a running desktop compositor. To boot to console:
     sudo raspi-config
     • System Options -> Boot / Auto Login -> Console (text console, no auto login)
   - If you prefer to keep the desktop, use the SDL backend instead (USE_SDL_BACKEND=1) and run inside your desktop session.

4) Fetch LVGL sources and drivers into this repo
   make setup

   This clones lvgl/ and lv_drivers/ directories and copies lv_conf.h and lv_drv_conf.h into them as expected by the build.

5) Build
   make

6) Run
   - Requires access to /dev/fb0 and an input device (default: /dev/input/event0) when using fbdev
   - On Raspberry Pi you'll typically need group membership: video, input, and i2c (for the BME280)
   - Raspberry Pi 4B + Wayland desktop: /dev/fb0 may not be accessible from user apps; prefer console mode or use the SDL backend when running under Wayland.

   Option A (simplest): run with sudo
   sudo ./weather_app

   Option B (recommended): add your user to the appropriate groups
   sudo usermod -aG video,input,i2c "$USER"
   newgrp video
   newgrp input
   newgrp i2c
   ./weather_app

   If the input device path is different (e.g., event2 for a touchscreen), edit lv_drv_conf.h and set EVDEV_NAME accordingly.

## Runtime configuration and tuning

- Display resolution
  • Defaults to 480x320 (see DISP_HOR_RES and DISP_VER_RES in main.c and LV_HOR_RES_MAX/LV_VER_RES_MAX in lv_conf.h)
  • Override at build time:
    - Raspberry Pi fbdev (default backend):
      make CFLAGS+="-DDISP_HOR_RES=800 -DDISP_VER_RES=480"
    - Desktop SDL backend (ensure both LVGL and the SDL window use the same size):
      make USE_SDL_BACKEND=1 CFLAGS+="-DDISP_HOR_RES=800 -DDISP_VER_RES=480 -DMONITOR_HOR_RES=800 -DMONITOR_VER_RES=480"
    - Optional: adjust DPI for SDL window scaling
      make USE_SDL_BACKEND=1 CFLAGS+="-DMONITOR_DPI=140"

  • Raspberry Pi 4B HDMI framebuffer sizing (optional):
    - If the HDMI panel does not expose a native 800x480 mode and you want a fixed framebuffer size, set a custom mode in /boot/firmware/config.txt, for example:
      sudo nano /boot/firmware/config.txt
      # Add near the end (example for 800x480@60)
      hdmi_group=2
      hdmi_mode=87
      hdmi_cvt=800 480 60 6 0 0 0
      # Reboot to apply

- UI update and API refresh intervals
  • UI_UPDATE_INTERVAL_MS (default 1000 ms) controls how often the GUI thread refreshes labels
  • API_REFRESH_SEC (default 30 s) controls background data refresh
  • Override at build time, for example:
    make CFLAGS+="-DUI_UPDATE_INTERVAL_MS=500 -DAPI_REFRESH_SEC=10"

- Input device selection (Raspberry Pi)
  • Edit lv_drv_conf.h and set EVDEV_NAME to the correct input device path
  • Find your device: ls -l /dev/input/by-id/ or ls -l /dev/input/
  • On the official 7" touchscreen, pointer input may appear as event0/1; keyboards usually appear as higher indices

- BME280 sensor integration (Raspberry Pi)
  • Enabled by default on Linux. Disable at build time with either of:
    - make DISABLE_BME280=1
    - make CFLAGS+="-DDISABLE_BME280=1"
  • Default I2C device path on Raspberry Pi: /dev/i2c-1. Override via environment or CLI:
    - Environment: BME280_I2C_DEV=/dev/i2c-0 ./weather_app
    - CLI: ./weather_app --i2c=/dev/i2c-0
  • Permissions: ensure your user is in the i2c group and that I2C is enabled via raspi-config.

- Permissions (Raspberry Pi)
  • If you see errors opening /dev/fb0 or /dev/input/eventX, run with sudo or add your user to the video and input groups

## Development setup

- Project dependencies
  • LVGL core and lv_drivers are pulled by: make setup
  • Portable sensor and bus libraries included in this repo: BME280 (C), minimal I2C/SPI abstractions
  • Compiler: GCC/Clang; Make
  • Optional libs for future real network APIs: libcurl, cJSON or similar

- Typical workflow
  1) Clone repo
  2) make setup (pulls lvgl and lv_drivers, copies configs)
  3) Build:
     • Raspberry Pi fbdev (default): make clean && make
     • Desktop SDL (Raspberry Pi desktop session): make clean && make USE_SDL_BACKEND=1
  4) Run:
     • Raspberry Pi fbdev (console): ./weather_app (may require sudo or group membership)
     • Desktop SDL (Raspberry Pi desktop): ./weather_app

- Code structure and threading
  • main.c launches two threads:
    - lvgl_tick_thread: increments lv_tick_inc(1) every 1 ms
    - api_update_thread: fetches data periodically and updates a shared struct
  • A GUI-thread lv_timer (update_display_timer_cb) runs every UI_UPDATE_INTERVAL_MS and updates labels safely on the GUI thread.
  • LVGL is not thread-safe; keep LVGL API calls on the GUI thread.

- Extending to real APIs
  • Replace get_*_from_api functions with network calls (e.g., libcurl) and JSON parsing
  • Install libs (Raspberry Pi OS): sudo apt-get install -y libcurl4-openssl-dev
  • Link by adding to LDFLAGS in the Makefile, e.g., LDFLAGS += -lcurl
  • Handle errors robustly; consider validity flags or NaN handling as in the demo

Troubleshooting
- Blank screen or permission errors
  • Check that /dev/fb0 exists and you have access (groups: video)
  • If /dev/fb0 is missing while running the desktop, either switch to console mode or use the SDL backend.
  • On Raspberry Pi 4B running Wayland, prefer SDL when in a desktop session.

- No input events
  • Verify EVDEV_NAME path in lv_drv_conf.h
  • Try another device (e.g., /dev/input/event1)

- SDL build issues (Raspberry Pi)
  • Ensure libsdl2-dev and pkg-config are installed (pkg-config --libs sdl2)

## Notes on platform

- Raspberry Pi: the framebuffer target is ideal for console-mode devices or headless setups with direct fbdev access. The SDL backend is suitable for running in a desktop session on Raspberry Pi OS.

## License

- This project uses LVGL, lv_drivers, and portable sensor/bus libraries under their respective licenses. Consult those repositories and headers for details.
