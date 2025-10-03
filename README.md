
LVGL Weather Station Demo

Overview
- A minimal weather dashboard built with LVGL (v8 style API) that targets the Linux framebuffer (fbdev) for display output and evdev for input. It renders four widgets (temperature, pressure, humidity, time) and periodically refreshes their data using a background thread that simulates weather API calls.
- The default configuration is Linux-only (fbdev/evdev). For macOS and Windows, use an SDL-based desktop backend or run under Linux/WSL.

Repository layout
- main.c: Application entry point, UI creation, threads, and HAL init
- Makefile: Build, clean, Linux dependency install, and LVGL/lv_drivers setup
- lv_conf.h: LVGL configuration (fonts, logging, resolution, etc.)
- lv_drv_conf.h: LVGL driver configuration (fbdev/evdev paths, etc.)

Build and run: quick start
Linux (Debian/Ubuntu, Raspberry Pi, etc.)
1) Install prerequisites
   sudo apt-get update
   sudo apt-get install -y build-essential git

   Notes
   - fbdev and evdev are kernel/driver interfaces; they don't require SDL on a headless target. If you plan to switch to SDL (desktop), also install: sudo apt-get install -y libsdl2-dev
   - New: You can build the desktop/SDL variant without editing code by adding the flag USE_SDL_BACKEND=1 to make. Example:
     sudo apt-get install -y libsdl2-dev pkg-config
     make clean && make USE_SDL_BACKEND=1

2) Fetch LVGL sources and drivers into this repo
   make setup

   This clones lvgl/ and lv_drivers/ directories and copies lv_conf.h and lv_drv_conf.h into them as expected by the build.

3) Build
   make

4) Run
   - Requires access to /dev/fb0 and an input device (default: /dev/input/event0)
   - On many systems you'll need elevated permissions or group membership: video and input

   Option A (simplest): run with sudo
   sudo ./weather_app

   Option B (recommended): add your user to the appropriate groups
   sudo usermod -aG video,input "$USER"
   newgrp video
   newgrp input
   ./weather_app

   If the input device path is different (e.g., event2), edit lv_drv_conf.h and set EVDEV_NAME accordingly.

macOS
The default fbdev/evdev configuration is Linux-specific. On macOS, you have two options:
- Option A (recommended for macOS): build with the SDL backend (desktop window)
- Option B: run a Linux VM/Container with framebuffer support and use the Linux steps

Option A: SDL desktop build (macOS)
1) Install Xcode Command Line Tools and Homebrew
   xcode-select --install
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

2) Install tools and SDL2
   brew install git make gcc sdl2 pkg-config

3) Fetch LVGL sources and drivers into this repo
   make setup

4) Build and run with SDL backend (no code edits needed)
   make clean && make USE_SDL_BACKEND=1
   ./weather_app

Windows
On Windows you can:
- Option A (simplest): use WSL2 (Ubuntu) and follow the Linux steps (recommended if you just want to run it)
- Option B: build natively with MSYS2 + SDL backend

Option A: WSL2 (Ubuntu)
1) Install WSL2 and Ubuntu from the Microsoft Store
2) Inside Ubuntu (WSL2), install prerequisites
   sudo apt-get update
   sudo apt-get install -y build-essential git libsdl2-dev
3) In your WSL2 home or mounted workspace, clone this repo and run
   make setup
   make
4) Run
   - fbdev/evdev requires /dev/fb0 and /dev/input/*, which are typically not available in WSL. Prefer SDL for WSL: follow the SDL steps from macOS above (adjust lv_drv_conf.h and main.c). Then simply run ./weather_app to open a window via the Windows X/Wayland bridge used by SDL.

Option B: Native Windows with MSYS2 + SDL
1) Install MSYS2 from https://www.msys2.org
2) Open the "MSYS2 UCRT64" terminal and install toolchain and SDL2
   pacman -Syu
   pacman -S --needed git make
   pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2
3) Ensure you are in the UCRT64 environment (check that which gcc shows the mingw-w64-ucrt build)
4) Clone this repo and run
   make setup
5) Build and run with SDL backend
   make clean && make USE_SDL_BACKEND=1
   ./weather_app.exe

Runtime configuration and tuning
- Display resolution
  • Defaults to 480x320 (see DISP_HOR_RES and DISP_VER_RES in main.c and LV_HOR_RES_MAX/LV_VER_RES_MAX in lv_conf.h)
  • Override at build time:
    - Linux fbdev (default backend):
      make CFLAGS+="-DDISP_HOR_RES=800 -DDISP_VER_RES=480"
    - Desktop SDL backend (ensure both LVGL and the SDL window use the same size):
      make USE_SDL_BACKEND=1 CFLAGS+="-DDISP_HOR_RES=800 -DDISP_VER_RES=480 -DMONITOR_HOR_RES=800 -DMONITOR_VER_RES=480"
    - Optional: adjust DPI for SDL window scaling
      make USE_SDL_BACKEND=1 CFLAGS+="-DMONITOR_DPI=140"

- UI update and API refresh intervals
  • UI_UPDATE_INTERVAL_MS (default 1000 ms) controls how often the GUI thread refreshes labels
  • API_REFRESH_SEC (default 30 s) controls background data refresh
  • Override at build time, for example:
    make CFLAGS+="-DUI_UPDATE_INTERVAL_MS=500 -DAPI_REFRESH_SEC=10"

- Input device selection (Linux)
  • Edit lv_drv_conf.h and set EVDEV_NAME to the correct input device path
  • Find your device: ls -l /dev/input/by-id/ or ls -l /dev/input/

- Permissions (Linux)
  • If you see errors opening /dev/fb0 or /dev/input/eventX, run with sudo or add your user to the video and input groups

Development setup
- Project dependencies
  • LVGL core and lv_drivers are pulled by: make setup
  • Compiler: GCC/Clang; Make
  • Optional libs for future real APIs: libcurl, cJSON or similar

- Typical workflow
  1) Clone repo
  2) make setup (pulls lvgl and lv_drivers, copies configs)
  3) Build:
     • Linux fbdev (default): make clean && make
     • Desktop SDL:          make clean && make USE_SDL_BACKEND=1
  4) Run:
     • Linux fbdev: ./weather_app (may require sudo or group membership)
     • Desktop SDL: ./weather_app (macOS/Linux) or ./weather_app.exe (Windows)

- Code structure and threading
  • main.c launches two threads:
    - lvgl_tick_thread: increments lv_tick_inc(1) every 1 ms
    - api_update_thread: simulates fetching data every 30 s and updates a shared struct
  • A GUI-thread lv_timer (update_display_timer_cb) runs every UI_UPDATE_INTERVAL_MS and updates labels safely on the GUI thread.
  • LVGL is not thread-safe; keep LVGL API calls on the GUI thread.

- Extending to real APIs
  • Replace get_*_from_api functions with network calls (e.g., libcurl) and JSON parsing
  • Install libs (Linux): sudo apt-get install -y libcurl4-openssl-dev
  • Link by adding to LDFLAGS in the Makefile, e.g., LDFLAGS += -lcurl
  • Handle errors robustly; consider validity flags or NaN handling as in the demo

Troubleshooting
- Blank screen or permission errors (Linux)
  • Check that /dev/fb0 exists and you have access (groups: video)
  • Ensure your system is not running a conflicting compositor on the same framebuffer

- No input events
  • Verify EVDEV_NAME path in lv_drv_conf.h
  • Try another device (e.g., /dev/input/event1)

- Build errors related to SDL on non-Linux
  • Build with: make USE_SDL_BACKEND=1
  • Verify SDL2 is installed and discoverable (brew doctor; pkg-config --libs sdl2)
  • If pkg-config can't find SDL2, set PKG_CONFIG_PATH accordingly (e.g., export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig" on macOS if needed)

Notes on platforms
- Linux framebuffer target is ideal for embedded devices, Raspberry Pi (console mode), or headless systems with direct fbdev access.
- macOS and Windows are best served with an SDL or similar desktop backend. The repository is kept defaulted to Linux fbdev for simplicity; SDL config requires minor code changes as described above.

License
- This project uses LVGL and lv_drivers under their respective licenses. Consult those repositories for details.
