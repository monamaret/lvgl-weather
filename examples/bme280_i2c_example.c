#if defined(__linux__)

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "BME280.h"
#include "BME280_I2CDevice.h"
#include "I2CDevice.h"
#include "Sensor.h"

static uint64_t now_millis(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static uint8_t parse_addr(const char *s, uint8_t def) {
    if (!s || !*s) return def;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 0);
    if (end == s || v > 0x7F) return def;
    return (uint8_t)v;
}

int main(int argc, char **argv) {
    const char *i2c_path = (argc > 1) ? argv[1] : "/dev/i2c-1";
    uint8_t addr = (argc > 2) ? parse_addr(argv[2], BME280_I2C_ADDR_SDO_LOW) : BME280_I2C_ADDR_SDO_LOW;

    I2CDevice i2c;
    if (i2c_device_open(&i2c, i2c_path, addr) != 0) {
        perror("i2c_device_open");
        return 1;
    }

    bme280_t bme;
    int rc = bme280_init_i2c_linux(&bme, &i2c, addr);
    if (rc != BME280_OK) {
        fprintf(stderr, "bme280_init failed: %d\n", rc);
        i2c_device_close(&i2c);
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
    bme280_make_temperature_sensor(&temp_ctx, &bme, &temp_if, 1001);
    bme280_make_pressure_sensor(&pres_ctx, &bme, &pres_if, 1002);
    bme280_make_humidity_sensor(&hum_ctx, &bme, &hum_if, 1003);

    printf("Reading BME280 on %s addr 0x%02X (Ctrl+C to stop)\n", i2c_path, addr);

    for (;;) {
        sensors_event_t te = {0}, pe = {0}, he = {0};
        bool ok_t = sensor_get_event(&temp_if, &te);
        bool ok_p = sensor_get_event(&pres_if, &pe);
        bool ok_h = sensor_get_event(&hum_if, &he);
        uint64_t tms = now_millis();
        (void)tms; // not strictly needed for printing below

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
    i2c_device_close(&i2c);
    return 0;
}

#else
int main(void) {
    fprintf(stderr, "This example requires Linux with /dev/i2c-*.\n");
    return 1;
}
#endif
