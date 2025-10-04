#ifndef I2C_DEVICE_H
#define I2C_DEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

// A minimal, Linux-friendly replacement for Arduino-specific I2C helpers.
// This header provides a C API for working with I2C devices on Linux via
// the /dev/i2c-* interface and does not depend on Arduino headers/APIs.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#if defined(__linux__)
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#else
#error "I2CDevice.h is intended for Linux systems using /dev/i2c-*"
#endif

#ifndef I2C_DEVICE_PATH_MAX
#define I2C_DEVICE_PATH_MAX 64
#endif

// Represents an open I2C device on Linux.
typedef struct {
    int fd;                 // File descriptor for /dev/i2c-X
    uint16_t addr;          // Device address (7-bit or 10-bit)
    int tenbit;             // Non-zero if using 10-bit addressing
    char path[I2C_DEVICE_PATH_MAX]; // Path to the device (e.g., "/dev/i2c-1")
} I2CDevice;

static inline void i2c_device_clear(I2CDevice *dev) {
    if (!dev) return;
    dev->fd = -1;
    dev->addr = 0;
    dev->tenbit = 0;
    dev->path[0] = '\0';
}

// Open and configure an I2C device at the given path and address.
// Returns 0 on success, -1 on error with errno set.
static inline int i2c_device_open(I2CDevice *dev, const char *path, uint16_t addr) {
    if (!dev || !path) { errno = EINVAL; return -1; }
    i2c_device_clear(dev);

    int fd = open(path, O_RDWR);
    if (fd < 0) return -1;

    dev->fd = fd;
    dev->addr = addr;
    dev->tenbit = (addr > 0x7F) ? 1 : 0;

    if (ioctl(fd, I2C_TENBIT, dev->tenbit) < 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        i2c_device_clear(dev);
        return -1;
    }
    if (ioctl(fd, I2C_SLAVE, addr) < 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        i2c_device_clear(dev);
        return -1;
    }

    size_t n = strnlen(path, I2C_DEVICE_PATH_MAX - 1);
    memcpy(dev->path, path, n);
    dev->path[n] = '\0';
    return 0;
}

// Close the I2C device. Returns 0 on success, -1 on error with errno set.
static inline int i2c_device_close(I2CDevice *dev) {
    if (!dev) { errno = EINVAL; return -1; }
    int rc = 0;
    if (dev->fd >= 0) {
        rc = close(dev->fd);
    }
    i2c_device_clear(dev);
    return rc;
}

// Write raw bytes to the device. Returns number of bytes written or -1 on error.
static inline ssize_t i2c_device_write(const I2CDevice *dev, const void *data, size_t len) {
    if (!dev || dev->fd < 0 || (!data && len)) { errno = EINVAL; return -1; }
    return write(dev->fd, data, len);
}

// Read raw bytes from the device. Returns number of bytes read or -1 on error.
static inline ssize_t i2c_device_read(const I2CDevice *dev, void *data, size_t len) {
    if (!dev || dev->fd < 0 || (!data && len)) { errno = EINVAL; return -1; }
    return read(dev->fd, data, len);
}

// Perform a combined write-then-read transaction with a repeated start.
// Returns 0 on success, -1 on error with errno set.
static inline int i2c_device_write_read(const I2CDevice *dev,
                                        const void *wbuf, size_t wlen,
                                        void *rbuf, size_t rlen) {
    if (!dev || dev->fd < 0) { errno = EINVAL; return -1; }
    if (wlen == 0 && rlen == 0) return 0;

    struct i2c_rdwr_ioctl_data rdwr;
    struct i2c_msg msgs[2];
    int nmsgs = 0;

    if (wlen) {
        msgs[nmsgs].addr  = dev->addr;
        msgs[nmsgs].flags = dev->tenbit ? I2C_M_TEN : 0;
        msgs[nmsgs].len   = (uint16_t)wlen;
        msgs[nmsgs].buf   = (uint8_t *)wbuf; // kernel will not modify
        nmsgs++;
    }
    if (rlen) {
        msgs[nmsgs].addr  = dev->addr;
        msgs[nmsgs].flags = (dev->tenbit ? I2C_M_TEN : 0) | I2C_M_RD;
        msgs[nmsgs].len   = (uint16_t)rlen;
        msgs[nmsgs].buf   = (uint8_t *)rbuf;
        nmsgs++;
    }

    rdwr.msgs  = msgs;
    rdwr.nmsgs = nmsgs;

    if (ioctl(dev->fd, I2C_RDWR, &rdwr) < 0) return -1;
    return 0;
}

// Read from a 1 or 2-byte register address using a repeated start.
// reg_width_bytes must be 1 or 2. Register value is sent MSB first when 2 bytes.
// Returns 0 on success, -1 on error with errno set.
static inline int i2c_device_read_reg(const I2CDevice *dev,
                                      uint16_t reg, void *buf, size_t len,
                                      int reg_width_bytes) {
    if (!dev || dev->fd < 0 || (!buf && len)) { errno = EINVAL; return -1; }
    if (reg_width_bytes != 1 && reg_width_bytes != 2) { errno = EINVAL; return -1; }

    uint8_t rbuf[2];
    size_t wlen = (size_t)reg_width_bytes;

    if (reg_width_bytes == 1) {
        rbuf[0] = (uint8_t)reg;
    } else {
        // Big-endian register address (MSB first)
        rbuf[0] = (uint8_t)((reg >> 8) & 0xFF);
        rbuf[1] = (uint8_t)(reg & 0xFF);
    }

    return i2c_device_write_read(dev, rbuf, wlen, buf, len);
}

// Write to a 1 or 2-byte register address (reg MSB first when 2 bytes).
// Returns 0 on success, -1 on error with errno set.
static inline int i2c_device_write_reg(const I2CDevice *dev,
                                       uint16_t reg, const void *data, size_t len,
                                       int reg_width_bytes) {
    if (!dev || dev->fd < 0 || (!data && len)) { errno = EINVAL; return -1; }
    if (reg_width_bytes != 1 && reg_width_bytes != 2) { errno = EINVAL; return -1; }

    size_t wlen = (size_t)reg_width_bytes + len;
    uint8_t stackbuf[2 + 64];
    uint8_t *wbuf = NULL;
    bool use_stack = (wlen <= sizeof(stackbuf));

    if (use_stack) {
        wbuf = stackbuf;
    } else {
        wbuf = (uint8_t *)malloc(wlen);
        if (!wbuf) { errno = ENOMEM; return -1; }
    }

    if (reg_width_bytes == 1) {
        wbuf[0] = (uint8_t)reg;
    } else {
        wbuf[0] = (uint8_t)((reg >> 8) & 0xFF);
        wbuf[1] = (uint8_t)(reg & 0xFF);
    }

    if (len) memcpy(wbuf + reg_width_bytes, data, len);

    ssize_t wr = i2c_device_write(dev, wbuf, wlen);
    int saved_errno = errno;

    if (!use_stack) free(wbuf);

    if (wr < 0 || (size_t)wr != wlen) { errno = saved_errno ? saved_errno : EIO; return -1; }
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif // I2C_DEVICE_H
