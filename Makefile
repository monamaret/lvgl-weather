# Compiler and flags
CC = gcc
CFLAGS = -O3 -g0 -Wall -Wextra -Wshadow -Wundef -Wmissing-prototypes -Wno-discarded-qualifiers -Wno-unused-function -Wno-error=strict-prototypes -Wpointer-arith -fno-strict-aliasing -Wno-error=cpp -Wuninitialized -Wmaybe-uninitialized -Wno-unused-parameter -Wno-missing-field-initializers -Wtype-limits -Wsizeof-pointer-memaccess -Wno-format-nonliteral -Wno-cast-qual -Wunreachable-code -Wno-switch-default -Wreturn-type -Wmultichar -Wformat-security -Wno-ignored-qualifiers -Wno-error=pedantic -Wno-sign-compare -Wno-error=missing-prototypes -Wdouble-promotion -Wclobbered -Wdeprecated -Wempty-body -Wshift-negative-value -Wstack-usage=2048

# Libraries
LDFLAGS = -lm -lpthread

# LVGL configuration
LVGL_DIR_NAME = lvgl
LVGL_DIR = ${shell pwd}

# Include paths
CFLAGS += -I$(LVGL_DIR)/ -I$(LVGL_DIR)/src/

# Source files
include $(LVGL_DIR)/lvgl/lvgl.mk
include $(LVGL_DIR)/lv_drivers/lv_drivers.mk

CSRCS += main.c

# Object files
OBJEXT = .o
AOBJS = $(ASRCS:.S=$(OBJEXT))
COBJS = $(CSRCS:.c=$(OBJEXT))
OBJS = $(AOBJS) $(COBJS)

# Binary name
BIN = weather_app

.PHONY: clean

default: $(BIN)

$(BIN): $(OBJS)
	@$(CC) -o $(BIN) $(OBJS) $(LDFLAGS)
	@echo "Build complete: $(BIN)"

%.o: %.c
	@echo "Compiling: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

clean:
	@rm -f $(BIN) $(OBJS)
	@echo "Clean complete"

install:
	@echo "Installing dependencies..."
	sudo apt-get update
	sudo apt-get install -y build-essential cmake git libsdl2-dev

setup:
	@echo "Setting up LVGL and drivers..."
	git clone --recursive https://github.com/lvgl/lvgl.git
	git clone https://github.com/lvgl/lv_drivers.git
	cp lv_conf.h lvgl/
	cp lv_drv_conf.h lv_drivers/

# SDL backend support (optional): compile with -DUSE_SDL_BACKEND to switch HAL to SDL
ifdef USE_SDL_BACKEND
CFLAGS  += -DUSE_SDL_BACKEND $(shell pkg-config --cflags sdl2 2>/dev/null)
LDFLAGS += $(shell pkg-config --libs sdl2 2>/dev/null)
endif