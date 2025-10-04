#ifndef BME280_I2CDEVICE_H
#define BME280_I2CDEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Linux I2C adapter for the portable BME280 driver.
 * Bridges I2CDevice.h to bme280_bus_t callbacks.
 * Build only on Linux targets; the implementation is guarded with __linux__.
 */

#include "BME280.h"

#if defined(__linux__)
#include "I2CDevice.h"

/* Initialize a bme280_bus_t with callbacks backed by an I2CDevice. */
void bme280_bus_from_i2c_device(bme280_bus_t *out, I2CDevice *dev);

/* Convenience initializer: wraps bus setup + bme280_init */
int bme280_init_i2c_linux(bme280_t *bme, I2CDevice *i2c, uint8_t i2c_addr);

#else
/* Stubs for non-Linux to avoid including Linux-only headers */
#warning "BME280_I2CDevice.h included on non-Linux target; no declarations emitted."
#endif

#ifdef __cplusplus
}
#endif

#endif /* BME280_I2CDEVICE_H */
