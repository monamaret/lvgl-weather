#include "BME280_SPIDevice.h"

#if defined(__linux__)

#include <string.h>
#include <time.h>
#include <stdlib.h>

static int bme280_spi_read(void *user, uint8_t reg, uint8_t *buf, size_t len) {
    SPIDevice *dev = (SPIDevice *)user;
    uint8_t cmd = reg | 0x80; // MSB=1 for read per BME280 SPI protocol
    int rc = spi_device_write_then_read(dev, &cmd, 1, buf, len);
    return (rc == 0) ? BME280_OK : BME280_E_COMM;
}

static int bme280_spi_write(void *user, uint8_t reg, const uint8_t *buf, size_t len) {
    SPIDevice *dev = (SPIDevice *)user;
    uint8_t stack[1 + 32];
    uint8_t *w = stack;
    size_t wlen = 1 + len;
    uint8_t *alloc = NULL;
    if (wlen > sizeof(stack)) {
        alloc = (uint8_t*)malloc(wlen);
        if (!alloc) return BME280_E_COMM;
        w = alloc;
    }
    w[0] = reg & 0x7F; // MSB=0 for write
    if (len && buf) memcpy(&w[1], buf, len);
    int rc = spi_device_write(dev, w, wlen);
    if (alloc) free(alloc);
    return (rc == 0) ? BME280_OK : BME280_E_COMM;
}

static void bme280_spi_delay(void *user, uint32_t ms) {
    (void)user;
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

void bme280_bus_from_spi_device(bme280_bus_t *out, SPIDevice *dev) {
    if (!out) return;
    out->read = bme280_spi_read;
    out->write = bme280_spi_write;
    out->delay_ms = bme280_spi_delay;
    out->user = dev;
}

int bme280_init_spi_linux(bme280_t *bme, SPIDevice *spi) {
    if (!bme || !spi) return BME280_E_NULL_PTR;
    bme280_bus_t bus;
    bme280_bus_from_spi_device(&bus, spi);
    return bme280_init(bme, &bus, 0);
}

#endif /* __linux__ */
