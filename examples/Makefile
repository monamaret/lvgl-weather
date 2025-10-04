# Makefile for building BME280 examples on Linux

CC      ?= cc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS  ?=

# Include headers from project root
INCLUDES := -I..

# Core driver sources (relative to this Makefile)
CORE_SRCS := ../BME280.c
I2C_SRCS  := ../BME280_I2CDevice.c
SPI_SRCS  := ../BME280_SPIDevice.c

EX_I2C   := bme280_i2c_example.c
EX_SPI   := bme280_spi_example.c

TARGETS := bme280_i2c bme280_spi

all: $(TARGETS)

bme280_i2c: $(EX_I2C) $(CORE_SRCS) $(I2C_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS) $(LDLIBS)

bme280_spi: $(EX_SPI) $(CORE_SRCS) $(SPI_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS) $(LDLIBS)

clean:
	$(RM) $(TARGETS)

.PHONY: all clean
