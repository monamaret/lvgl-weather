# BME280 Linux examples

This directory contains minimal examples for reading a Bosch BME280 sensor on Linux, using the portable driver in the project root and the Linux I2C/SPI adapters.

Examples:
- bme280_i2c_example.c — read over /dev/i2c-X
- bme280_spi_example.c — read over /dev/spidevX.Y

Both examples expose readings through the Sensor.h interface and print Temperature (°C), Pressure (hPa), and Humidity (%RH) once per second.

## Requirements
- Linux system with I2C and/or SPI userspace interfaces exposed:
  - I2C: /dev/i2c-*
  - SPI: /dev/spidevX.Y
- Build tools (gcc or clang, make)
- Header sources are included from the project root (no extra libraries required)

On Debian/Ubuntu/Raspberry Pi OS:
- sudo apt-get update
- sudo apt-get install -y build-essential

Raspberry Pi notes:
- Enable I2C and/or SPI via raspi-config: Interface Options -> I2C/SPI -> Enable
- Reboot after enabling

## Build

Use the examples Makefile:

```
cd examples
make bme280_i2c
make bme280_spi
```

This produces two binaries in the examples directory:
- ./bme280_i2c
- ./bme280_spi

Notes:
- On some older systems, linking may require -lrt for clock_gettime. If needed, build with:
  - make LDLIBS="-lrt"
- If you prefer clang:
  - make CC=clang

## Run

### I2C example
```
./bme280_i2c [i2c_device_path] [i2c_addr]
```
Defaults:
- i2c_device_path = /dev/i2c-1
- i2c_addr = 0x76 (BME280_I2C_ADDR_SDO_LOW)

Examples:
- ./bme280_i2c
- ./bme280_i2c /dev/i2c-1 0x77

### SPI example
```
./bme280_spi [spidev_path] [speed_hz] [mode]
```
Defaults:
- spidev_path = /dev/spidev0.0
- speed_hz = 8000000 (8 MHz)
- mode = 0 (SPI_MODE_0)

Examples:
- ./bme280_spi
- ./bme280_spi /dev/spidev0.0 4000000 0
- ./bme280_spi /dev/spidev1.0 1000000 3

## Configuration details

- The examples configure the BME280 with oversampling x1 for T/P/H, filter off, standby 1000 ms, and NORMAL mode. You can adjust this in the example code:
  - bme280_set_oversampling(&bme, BME280_OSRS_X1, BME280_OSRS_X1, BME280_OSRS_X1);
  - bme280_set_filter(&bme, BME280_FILTER_OFF);
  - bme280_set_standby(&bme, BME280_STANDBY_1000_MS);
  - bme280_set_mode(&bme, BME280_NORMAL_MODE);
- For one-shot measurements, use FORCED mode instead of NORMAL and re-trigger between reads.

## Permissions
Access to /dev/i2c-* and /dev/spidev* may require elevated permissions.
- Option 1: run with sudo
- Option 2 (recommended): add your user to the relevant groups and re-login
  - sudo usermod -aG i2c $USER
  - sudo usermod -aG spi $USER

## Troubleshooting
- No such file or directory: ensure /dev/i2c-* or /dev/spidev* exist and are enabled (raspi-config on Raspberry Pi).
- I/O error on I2C: verify sensor address (0x76 vs 0x77) and wiring (SDA/SCL lines, pull-ups).
- No readings / constant zeros: check power (3.3V), GND, and that CSB/SDO pins are set correctly for the selected bus/address.
- SPI garbage: reduce speed_hz, confirm mode=0, and wiring (MOSI/MISO/SCLK/CS). BME280 SPI uses MSB=1 for reads and MSB=0 for writes; the adapter handles this.

## Code structure
- examples/bme280_i2c_example.c
- examples/bme280_spi_example.c
- examples/Makefile
- Core driver and adapters used by examples:
  - ../BME280.h, ../BME280.c
  - ../BME280_I2CDevice.h, ../BME280_I2CDevice.c
  - ../BME280_SPIDevice.h, ../BME280_SPIDevice.c
  - ../Sensor.h

The examples are self-contained and do not depend on Arduino libraries.
