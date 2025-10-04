#ifndef BME280_H
#define BME280_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Portable C BME280 driver (no Arduino dependencies)
// Bus access is provided by user-defined callbacks.

// I2C default addresses
#define BME280_I2C_ADDR_SDO_LOW   0x76
#define BME280_I2C_ADDR_SDO_HIGH  0x77

// Register map
#define BME280_REG_ID             0xD0
#define BME280_REG_RESET          0xE0
#define BME280_REG_CTRL_HUM       0xF2
#define BME280_REG_STATUS         0xF3
#define BME280_REG_CTRL_MEAS      0xF4
#define BME280_REG_CONFIG         0xF5
#define BME280_REG_PRESS_MSB      0xF7
#define BME280_REG_PRESS_LSB      0xF8
#define BME280_REG_PRESS_XLSB     0xF9
#define BME280_REG_TEMP_MSB       0xFA
#define BME280_REG_TEMP_LSB       0xFB
#define BME280_REG_TEMP_XLSB      0xFC
#define BME280_REG_HUM_MSB        0xFD
#define BME280_REG_HUM_LSB        0xFE

// Calibration register ranges
#define BME280_CALIB00_START      0x88  // 0x88..0xA1
#define BME280_CALIB00_END        0xA1
#define BME280_CALIB26_START      0xE1  // 0xE1..0xE7
#define BME280_CALIB26_END        0xE7

// Reset value
#define BME280_SOFT_RESET         0xB6

// Chip ID for BME280
#define BME280_CHIP_ID            0x60

// Status bits
#define BME280_STATUS_MEASURING   0x08
#define BME280_STATUS_IM_UPDATE   0x01

// Oversampling settings
typedef enum {
    BME280_OSRS_SKIP = 0,
    BME280_OSRS_X1   = 1,
    BME280_OSRS_X2   = 2,
    BME280_OSRS_X4   = 3,
    BME280_OSRS_X8   = 4,
    BME280_OSRS_X16  = 5
} bme280_oversampling_t;

// Filter coefficient
typedef enum {
    BME280_FILTER_OFF = 0,
    BME280_FILTER_2   = 1,
    BME280_FILTER_4   = 2,
    BME280_FILTER_8   = 3,
    BME280_FILTER_16  = 4
} bme280_filter_t;

// Standby time (in normal mode)
typedef enum {
    BME280_STANDBY_0_5_MS  = 0, // 0.5 ms
    BME280_STANDBY_62_5_MS = 1, // 62.5 ms
    BME280_STANDBY_125_MS  = 2, // 125 ms
    BME280_STANDBY_250_MS  = 3, // 250 ms
    BME280_STANDBY_500_MS  = 4, // 500 ms
    BME280_STANDBY_1000_MS = 5, // 1000 ms
    BME280_STANDBY_10_MS   = 6, // 10 ms
    BME280_STANDBY_20_MS   = 7  // 20 ms
} bme280_standby_t;

// Power mode
typedef enum {
    BME280_SLEEP_MODE  = 0,
    BME280_FORCED_MODE = 1,
    BME280_NORMAL_MODE = 3
} bme280_mode_t;

// Error/status codes
#define BME280_OK                  0
#define BME280_E_NULL_PTR         -1
#define BME280_E_COMM             -2
#define BME280_E_INVALID_ARG      -3
#define BME280_E_CHIP_ID_MISMATCH -4

// Bus callback signatures
// Return 0 on success, negative error on failure.
typedef int (*bme280_bus_read_f)(void *user, uint8_t reg, uint8_t *buf, size_t len);
typedef int (*bme280_bus_write_f)(void *user, uint8_t reg, const uint8_t *buf, size_t len);
typedef void (*bme280_delay_ms_f)(void *user, uint32_t ms);

// Bus abstraction provided by the host
typedef struct {
    bme280_bus_read_f  read;
    bme280_bus_write_f write;
    bme280_delay_ms_f  delay_ms; // Optional but recommended for polling; can be NULL
    void              *user;      // Opaque user context (e.g., I2C handle)
} bme280_bus_t;

// Calibration parameters from NVM (datasheet names)
typedef struct {
    uint16_t dig_T1; int16_t dig_T2; int16_t dig_T3;
    uint16_t dig_P1; int16_t dig_P2; int16_t dig_P3; int16_t dig_P4; int16_t dig_P5; int16_t dig_P6; int16_t dig_P7; int16_t dig_P8; int16_t dig_P9;
    uint8_t  dig_H1; int16_t dig_H2; uint8_t dig_H3; int16_t dig_H4; int16_t dig_H5; int8_t dig_H6;
} bme280_calib_t;

// Sensor configuration
typedef struct {
    bme280_oversampling_t osr_t;  // temperature OSRS
    bme280_oversampling_t osr_p;  // pressure OSRS
    bme280_oversampling_t osr_h;  // humidity OSRS
    bme280_filter_t       filter; // IIR filter
    bme280_standby_t      standby;// standby time in normal mode
    bme280_mode_t         mode;   // current power mode
} bme280_settings_t;

// Device context
typedef struct {
    bme280_bus_t       bus;
    uint8_t            i2c_addr;        // Only used for I2C implementations (bus callbacks may ignore)
    bme280_calib_t     calib;
    bme280_settings_t  settings;
    int32_t            t_fine;          // for compensation
    bool               calib_loaded;
} bme280_t;

// A single compensated reading (SI units)
typedef struct {
    float temperature_c; // degrees Celsius
    float pressure_pa;   // Pascals
    float humidity_rh;   // % relative humidity (0..100)
} bme280_reading_t;

// API
int bme280_init(bme280_t *dev, const bme280_bus_t *bus, uint8_t i2c_addr);
int bme280_soft_reset(bme280_t *dev);
int bme280_read_chip_id(bme280_t *dev, uint8_t *chip_id);
int bme280_read_calibration(bme280_t *dev);

// Configuration helpers
int bme280_set_oversampling(bme280_t *dev, bme280_oversampling_t osr_t, bme280_oversampling_t osr_p, bme280_oversampling_t osr_h);
int bme280_set_filter(bme280_t *dev, bme280_filter_t filter);
int bme280_set_standby(bme280_t *dev, bme280_standby_t standby);
int bme280_set_mode(bme280_t *dev, bme280_mode_t mode);

// Read raw ADC values (20-bit pressure/temperature, 16-bit humidity)
int bme280_read_raw(bme280_t *dev, int32_t *adc_T, int32_t *adc_P, int32_t *adc_H);

// Compensate raw values to SI units
float bme280_compensate_temperature(bme280_t *dev, int32_t adc_T);
float bme280_compensate_pressure(bme280_t *dev, int32_t adc_P);
float bme280_compensate_humidity(bme280_t *dev, int32_t adc_H);

// Convenience: take one measurement according to current settings
// If in FORCED mode, this function will trigger a measurement and wait for completion.
int bme280_read_measurement(bme280_t *dev, bme280_reading_t *out);

// Optional: Adafruit Unified Sensor-style wrappers
// These helpers build sensor_interface_t wrappers for each quantity.
#include "Sensor.h"

typedef struct {
    bme280_t *dev;
    int32_t sensor_id;
    int type; // sensors_type_t
} bme280_sensor_wrapper_t;

void bme280_make_temperature_sensor(bme280_sensor_wrapper_t *ctx, bme280_t *dev, sensor_interface_t *iface, int32_t sensor_id);
void bme280_make_pressure_sensor(bme280_sensor_wrapper_t *ctx, bme280_t *dev, sensor_interface_t *iface, int32_t sensor_id);
void bme280_make_humidity_sensor(bme280_sensor_wrapper_t *ctx, bme280_t *dev, sensor_interface_t *iface, int32_t sensor_id);

#ifdef __cplusplus
}
#endif

#endif // BME280_H
