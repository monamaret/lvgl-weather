#ifndef BME280_SPIDEVICE_H
#define BME280_SPIDEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Linux SPI adapter for the portable BME280 driver.
 * Bridges SPIDevice.h to bme280_bus_t callbacks (3-wire command/data protocol with reg prefix).
 * Build only on Linux targets; the implementation is guarded with __linux__.
 */

#include "BME280.h"

#if defined(__linux__)
#include "SPIDevice.h"

/* Initialize a bme280_bus_t with callbacks backed by a SPIDevice.
 * Note: The BME280 SPI protocol expects the register address as the first byte.
 */
void bme280_bus_from_spi_device(bme280_bus_t *out, SPIDevice *dev);

/* Convenience initializer: wraps bus setup + bme280_init (i2c_addr ignored on SPI) */
int bme280_init_spi_linux(bme280_t *bme, SPIDevice *spi);

#else
#warning "BME280_SPIDevice.h included on non-Linux target; no declarations emitted."
#endif

#ifdef __cplusplus
}
#endif

#endif /* BME280_SPIDEVICE_H */
