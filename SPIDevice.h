#ifndef SPIDEVICE_H
#define SPIDEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
  SPIDevice.h
  Minimal, Arduino-free SPI device helper for Linux (spidev)

  Usage:
    #define SPIDEVICE_IMPLEMENTATION
    #include "SPIDevice.h"

  Example:
    SPIDevice dev;
    if (spi_device_open(&dev, "/dev/spidev0.0", 8000000u, 0 /* SPI_MODE_0 */, 8) == 0) {
      uint8_t tx[2] = { 0x0F, 0x00 };
      uint8_t rx[2];
      spi_device_write_then_read(&dev, tx, 1, rx, 1);
      spi_device_close(&dev);
    }

  Notes:
    - This header targets Linux systems exposing /dev/spidevX.Y.
    - No Arduino types or headers are used.
*/

#include <stdint.h>
#include <stddef.h>

/* Forward declaration of the device struct */
typedef struct SPIDevice {
  int fd;                 /* File descriptor for /dev/spidevX.Y */
  uint32_t speed_hz;      /* Max speed (Hz) */
  uint8_t mode;           /* SPI mode (0..3) plus option bits) */
  uint8_t bits_per_word;  /* Typically 8 */
  uint16_t delay_usecs;   /* Optional inter-transfer delay */
} SPIDevice;

/* Open and configure a spidev device. Returns 0 on success, -1 on error. */
int spi_device_open(SPIDevice *dev,
                    const char *device_path,
                    uint32_t speed_hz,
                    uint8_t mode,
                    uint8_t bits_per_word);

/* Close the device. Safe to call multiple times. Returns 0 on success, -1 on error. */
int spi_device_close(SPIDevice *dev);

/* Configuration setters. Each returns 0 on success, -1 on error. */
int spi_device_set_mode(SPIDevice *dev, uint8_t mode);
int spi_device_set_speed(SPIDevice *dev, uint32_t speed_hz);
int spi_device_set_bits_per_word(SPIDevice *dev, uint8_t bits_per_word);

/* Full-duplex transfer. If tx is NULL, zeros are sent. If rx is NULL, incoming bytes are discarded. */
int spi_device_transfer(SPIDevice *dev, const uint8_t *tx, uint8_t *rx, size_t len);

/* Convenience helpers */
int spi_device_write(SPIDevice *dev, const uint8_t *data, size_t len);
int spi_device_read(SPIDevice *dev, uint8_t *data, size_t len);
int spi_device_write_then_read(SPIDevice *dev,
                               const uint8_t *tx, size_t txlen,
                               uint8_t *rx, size_t rxlen);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ===== Implementation (define SPIDEVICE_IMPLEMENTATION before including) ===== */
#ifdef SPIDEVICE_IMPLEMENTATION

#ifndef __linux__
#  error "SPIDevice.h implementation requires Linux (spidev)."
#endif

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <stdlib.h>

/* Internal helper: clamp null pointers to a zero buffer when needed */
static inline const uint8_t *spi__zero_tx_buffer(size_t len) {
  if (len == 0) return NULL;
  /* Allocate a transient zero buffer; caller expects transfer to be synchronous */
  uint8_t *buf = (uint8_t *)calloc(1, len);
  return buf; /* Caller ensures freeing if allocated here */
}

int spi_device_open(SPIDevice *dev,
                    const char *device_path,
                    uint32_t speed_hz,
                    uint8_t mode,
                    uint8_t bits_per_word) {
  if (!dev || !device_path) {
    errno = EINVAL;
    return -1;
  }

  dev->fd = open(device_path, O_RDWR | O_CLOEXEC);
  if (dev->fd < 0) {
    return -1;
  }

  dev->speed_hz = speed_hz;
  dev->mode = mode;
  dev->bits_per_word = bits_per_word;
  dev->delay_usecs = 0;

  if (ioctl(dev->fd, SPI_IOC_WR_MODE, &dev->mode) == -1) goto error;
  if (ioctl(dev->fd, SPI_IOC_WR_BITS_PER_WORD, &dev->bits_per_word) == -1) goto error;
  if (ioctl(dev->fd, SPI_IOC_WR_MAX_SPEED_HZ, &dev->speed_hz) == -1) goto error;

  /* Read-back to confirm */
  uint8_t rd_mode = 0;
  uint8_t rd_bits = 0;
  uint32_t rd_speed = 0;
  if (ioctl(dev->fd, SPI_IOC_RD_MODE, &rd_mode) == -1) goto error;
  if (ioctl(dev->fd, SPI_IOC_RD_BITS_PER_WORD, &rd_bits) == -1) goto error;
  if (ioctl(dev->fd, SPI_IOC_RD_MAX_SPEED_HZ, &rd_speed) == -1) goto error;

  dev->mode = rd_mode;
  dev->bits_per_word = rd_bits ? rd_bits : dev->bits_per_word;
  dev->speed_hz = rd_speed ? rd_speed : dev->speed_hz;

  return 0;

error:
  {
    int saved = errno;
    close(dev->fd);
    dev->fd = -1;
    errno = saved;
    return -1;
  }
}

int spi_device_close(SPIDevice *dev) {
  if (!dev) {
    errno = EINVAL;
    return -1;
  }
  int rc = 0;
  if (dev->fd >= 0) {
    rc = close(dev->fd);
  }
  dev->fd = -1;
  return rc;
}

int spi_device_set_mode(SPIDevice *dev, uint8_t mode) {
  if (!dev || dev->fd < 0) { errno = EINVAL; return -1; }
  if (ioctl(dev->fd, SPI_IOC_WR_MODE, &mode) == -1) return -1;
  dev->mode = mode;
  return 0;
}

int spi_device_set_speed(SPIDevice *dev, uint32_t speed_hz) {
  if (!dev || dev->fd < 0) { errno = EINVAL; return -1; }
  if (ioctl(dev->fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz) == -1) return -1;
  dev->speed_hz = speed_hz;
  return 0;
}

int spi_device_set_bits_per_word(SPIDevice *dev, uint8_t bits_per_word) {
  if (!dev || dev->fd < 0) { errno = EINVAL; return -1; }
  if (ioctl(dev->fd, SPI_IOC_WR_BITS_PER_WORD, &bits_per_word) == -1) return -1;
  dev->bits_per_word = bits_per_word;
  return 0;
}

int spi_device_transfer(SPIDevice *dev, const uint8_t *tx, uint8_t *rx, size_t len) {
  if (!dev || dev->fd < 0) { errno = EINVAL; return -1; }
  if (len == 0) return 0;

  const uint8_t *txbuf = tx;
  uint8_t *alloc = NULL;
  if (!txbuf) {
    alloc = (uint8_t *)calloc(1, len);
    if (!alloc) return -1;
    txbuf = alloc;
  }

  struct spi_ioc_transfer tr;
  memset(&tr, 0, sizeof(tr));
  tr.tx_buf = (unsigned long) (uintptr_t) txbuf;
  tr.rx_buf = (unsigned long) (uintptr_t) rx;
  tr.len = (uint32_t)len;
  tr.delay_usecs = dev->delay_usecs;
  tr.speed_hz = dev->speed_hz;
  tr.bits_per_word = dev->bits_per_word;

  int rc = ioctl(dev->fd, SPI_IOC_MESSAGE(1), &tr);

  if (alloc) free(alloc);

  return (rc < 1) ? -1 : 0;
}

int spi_device_write(SPIDevice *dev, const uint8_t *data, size_t len) {
  return spi_device_transfer(dev, data, NULL, len);
}

int spi_device_read(SPIDevice *dev, uint8_t *data, size_t len) {
  return spi_device_transfer(dev, NULL, data, len);
}

int spi_device_write_then_read(SPIDevice *dev,
                               const uint8_t *tx, size_t txlen,
                               uint8_t *rx, size_t rxlen) {
  if (!dev || dev->fd < 0) { errno = EINVAL; return -1; }

  struct spi_ioc_transfer xfers[2];
  memset(&xfers, 0, sizeof(xfers));

  const uint8_t *txbuf = tx;
  uint8_t *alloc = NULL;
  if (txlen && !txbuf) {
    alloc = (uint8_t *)calloc(1, txlen);
    if (!alloc) return -1;
    txbuf = alloc;
  }

  /* First: write */
  xfers[0].tx_buf = (unsigned long) (uintptr_t) txbuf;
  xfers[0].len = (uint32_t)txlen;
  xfers[0].delay_usecs = dev->delay_usecs;
  xfers[0].speed_hz = dev->speed_hz;
  xfers[0].bits_per_word = dev->bits_per_word;
  xfers[0].cs_change = 0; /* Keep CS asserted across both transfers */

  /* Second: read */
  xfers[1].rx_buf = (unsigned long) (uintptr_t) rx;
  xfers[1].len = (uint32_t)rxlen;
  xfers[1].delay_usecs = dev->delay_usecs;
  xfers[1].speed_hz = dev->speed_hz;
  xfers[1].bits_per_word = dev->bits_per_word;
  xfers[1].cs_change = 0;

  int rc = ioctl(dev->fd, SPI_IOC_MESSAGE(2), xfers);

  if (alloc) free(alloc);

  return (rc < 1) ? -1 : 0;
}

#endif /* SPIDEVICE_IMPLEMENTATION */

#endif /* SPIDEVICE_H */
