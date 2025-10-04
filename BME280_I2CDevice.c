#include "BME280_I2CDevice.h"

#if defined(__linux__)

#include <time.h>

static int bme280_i2c_read(void *user, uint8_t reg, uint8_t *buf, size_t len) {
    I2CDevice *dev = (I2CDevice *)user;
    int rc = i2c_device_read_reg(dev, reg, buf, len, 1);
    return (rc == 0) ? BME280_OK : BME280_E_COMM;
}

static int bme280_i2c_write(void *user, uint8_t reg, const uint8_t *buf, size_t len) {
    I2CDevice *dev = (I2CDevice *)user;
    int rc = i2c_device_write_reg(dev, reg, buf, len, 1);
    return (rc == 0) ? BME280_OK : BME280_E_COMM;
}

static void bme280_i2c_delay(void *user, uint32_t ms) {
    (void)user;
    /* Sleep in milliseconds */
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

void bme280_bus_from_i2c_device(bme280_bus_t *out, I2CDevice *dev) {
    if (!out) return;
    out->read = bme280_i2c_read;
    out->write = bme280_i2c_write;
    out->delay_ms = bme280_i2c_delay;
    out->user = dev;
}

int bme280_init_i2c_linux(bme280_t *bme, I2CDevice *i2c, uint8_t i2c_addr) {
    if (!bme || !i2c) return BME280_E_NULL_PTR;
    bme280_bus_t bus;
    bme280_bus_from_i2c_device(&bus, i2c);
    return bme280_init(bme, &bus, i2c_addr);
}

#endif /* __linux__ */
