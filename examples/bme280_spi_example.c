#if defined(__linux__)

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "BME280.h"
#include "BME280_SPIDevice.h"

/* Provide SPIDevice implementation in this TU */
#define SPIDEVICE_IMPLEMENTATION
#include "SPIDevice.h"

#include "Sensor.h"

static uint64_t now_millis(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static uint32_t parse_u32(const char *s, uint32_t def) {
    if (!s || !*s) return def;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 0);
    if (end == s) return def;
    return (uint32_t)v;
}

static uint8_t parse_u8(const char *s, uint8_t def) {
    if (!s || !*s) return def;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 0);
    if (end == s || v > 0xFF) return def;
    return (uint8_t)v;
}

int main(int argc, char **argv) {
    const char *spi_path = (argc > 1) ? argv[1] : "/dev/spidev0.0";
    uint32_t speed_hz = (argc > 2) ? parse_u32(argv[2], 8000000u) : 8000000u; // 8 MHz
    uint8_t mode = (argc > 3) ? parse_u8(argv[3], 0u) : 0u; // SPI mode 0

    SPIDevice spi;
    if (spi_device_open(&spi, spi_path, speed_hz, mode, 8) != 0) {
        perror("spi_device_open");
        return 1;
    }

    bme280_t bme;
    int rc = bme280_init_spi_linux(&bme, &spi);
    if (rc != BME280_OK) {
        fprintf(stderr, "bme280_init_spi_linux failed: %d\n", rc);
        spi_device_close(&spi);
        return 1;
    }

    // Configure: oversampling x1, filter off, standby 1000ms, normal mode
    bme280_set_oversampling(&bme, BME280_OSRS_X1, BME280_OSRS_X1, BME280_OSRS_X1);
    bme280_set_filter(&bme, BME280_FILTER_OFF);
    bme280_set_standby(&bme, BME280_STANDBY_1000_MS);
    bme280_set_mode(&bme, BME280_NORMAL_MODE);

    // Build Sensor.h interfaces
    bme280_sensor_wrapper_t temp_ctx, pres_ctx, hum_ctx;
    sensor_interface_t temp_if = {0}, pres_if = {0}, hum_if = {0};
    bme280_make_temperature_sensor(&temp_ctx, &bme, &temp_if, 2001);
    bme280_make_pressure_sensor(&pres_ctx, &bme, &pres_if, 2002);
    bme280_make_humidity_sensor(&hum_ctx, &bme, &hum_if, 2003);

    printf("Reading BME280 via SPI on %s @ %u Hz mode %u (Ctrl+C to stop)\n", spi_path, speed_hz, mode);

    for (;;) {
        sensors_event_t te = {0}, pe = {0}, he = {0};
        bool ok_t = sensor_get_event(&temp_if, &te);
        bool ok_p = sensor_get_event(&pres_if, &pe);
        bool ok_h = sensor_get_event(&hum_if, &he);
        uint64_t tms = now_millis();
        (void)tms;

        if (ok_t && ok_p && ok_h) {
            printf("T: %6.2f C  P: %8.2f hPa  H: %5.1f %%RH\n",
                   te.value.temperature,
                   pe.value.pressure,
                   he.value.relative_humidity);
        } else {
            fprintf(stderr, "read failed (t=%d p=%d h=%d)\n", ok_t, ok_p, ok_h);
        }
        usleep(1000 * 1000); // 1s
    }

    // Unreachable in this loop; included for completeness
    spi_device_close(&spi);
    return 0;
}

#else
int main(void) {
    fprintf(stderr, "This example requires Linux with /dev/spidev*.\n");
    return 1;
}
#endif
