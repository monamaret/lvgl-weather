#include "BME280.h"

// Internal helpers
static int bme280_write_u8(bme280_t *dev, uint8_t reg, uint8_t val) {
    return dev && dev->bus.write ? dev->bus.write(dev->bus.user, reg, &val, 1) : BME280_E_NULL_PTR;
}

static int bme280_read_u8(bme280_t *dev, uint8_t reg, uint8_t *val) {
    if (!dev || !val || !dev->bus.read) return BME280_E_NULL_PTR;
    return dev->bus.read(dev->bus.user, reg, val, 1);
}

static int bme280_read_buf(bme280_t *dev, uint8_t reg, uint8_t *buf, size_t len) {
    if (!dev || !buf || !dev->bus.read) return BME280_E_NULL_PTR;
    return dev->bus.read(dev->bus.user, reg, buf, len);
}

static int bme280_update_bits(bme280_t *dev, uint8_t reg, uint8_t mask, uint8_t value) {
    uint8_t cur;
    int rc = bme280_read_u8(dev, reg, &cur);
    if (rc != BME280_OK) return rc;
    uint8_t newv = (cur & ~mask) | (value & mask);
    if (newv == cur) return BME280_OK;
    return bme280_write_u8(dev, reg, newv);
}

static void bme280_delay(bme280_t *dev, uint32_t ms) {
    if (dev && dev->bus.delay_ms) dev->bus.delay_ms(dev->bus.user, ms);
}

int bme280_read_chip_id(bme280_t *dev, uint8_t *chip_id) {
    if (!dev || !chip_id) return BME280_E_NULL_PTR;
    return bme280_read_u8(dev, BME280_REG_ID, chip_id);
}

int bme280_soft_reset(bme280_t *dev) {
    if (!dev) return BME280_E_NULL_PTR;
    int rc = bme280_write_u8(dev, BME280_REG_RESET, BME280_SOFT_RESET);
    if (rc != BME280_OK) return rc;
    // Wait for NVM copy, datasheet suggests 2 ms; we'll poll STATUS[0] (im_update)
    for (int i = 0; i < 20; ++i) {
        uint8_t st = 0;
        rc = bme280_read_u8(dev, BME280_REG_STATUS, &st);
        if (rc != BME280_OK) return rc;
        if ((st & BME280_STATUS_IM_UPDATE) == 0) return BME280_OK;
        bme280_delay(dev, 2);
    }
    return BME280_OK; // proceed even if status didn't clear
}

static uint16_t u16_le(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static int16_t s16_le(const uint8_t *p) { return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }

int bme280_read_calibration(bme280_t *dev) {
    if (!dev) return BME280_E_NULL_PTR;
    uint8_t buf1[26]; // 0x88 .. 0xA1 (26 bytes)
    int rc = bme280_read_buf(dev, BME280_CALIB00_START, buf1, sizeof(buf1));
    if (rc != BME280_OK) return rc;

    dev->calib.dig_T1 = u16_le(&buf1[0]);
    dev->calib.dig_T2 = s16_le(&buf1[2]);
    dev->calib.dig_T3 = s16_le(&buf1[4]);

    dev->calib.dig_P1 = u16_le(&buf1[6]);
    dev->calib.dig_P2 = s16_le(&buf1[8]);
    dev->calib.dig_P3 = s16_le(&buf1[10]);
    dev->calib.dig_P4 = s16_le(&buf1[12]);
    dev->calib.dig_P5 = s16_le(&buf1[14]);
    dev->calib.dig_P6 = s16_le(&buf1[16]);
    dev->calib.dig_P7 = s16_le(&buf1[18]);
    dev->calib.dig_P8 = s16_le(&buf1[20]);
    dev->calib.dig_P9 = s16_le(&buf1[22]);

    dev->calib.dig_H1 = buf1[24]; // 0xA1

    uint8_t buf2[7]; // 0xE1 .. 0xE7
    rc = bme280_read_buf(dev, BME280_CALIB26_START, buf2, sizeof(buf2));
    if (rc != BME280_OK) return rc;

    dev->calib.dig_H2 = s16_le(&buf2[0]);
    dev->calib.dig_H3 = buf2[2];
    // H4/H5 are packed 12-bit signed values; perform proper sign extension
    uint16_t h4_raw = ((uint16_t)buf2[3] << 4) | (buf2[4] & 0x0F);
    uint16_t h5_raw = ((uint16_t)buf2[5] << 4) | (buf2[4] >> 4);
    int16_t h4 = (h4_raw & 0x0800) ? (int16_t)(h4_raw | 0xF000) : (int16_t)h4_raw;
    int16_t h5 = (h5_raw & 0x0800) ? (int16_t)(h5_raw | 0xF000) : (int16_t)h5_raw;
    dev->calib.dig_H4 = h4;
    dev->calib.dig_H5 = h5;
    dev->calib.dig_H6 = (int8_t)buf2[6];

    dev->calib_loaded = true;
    return BME280_OK;
}

int bme280_init(bme280_t *dev, const bme280_bus_t *bus, uint8_t i2c_addr) {
    if (!dev || !bus) return BME280_E_NULL_PTR;
    dev->bus = *bus;
    dev->i2c_addr = i2c_addr;
    dev->t_fine = 0;
    dev->calib_loaded = false;

    uint8_t id = 0;
    int rc = bme280_read_chip_id(dev, &id);
    if (rc != BME280_OK) return rc;
    if (id != BME280_CHIP_ID) return BME280_E_CHIP_ID_MISMATCH;

    rc = bme280_soft_reset(dev);
    if (rc != BME280_OK) return rc;

    rc = bme280_read_calibration(dev);
    if (rc != BME280_OK) return rc;

    // Default settings: osrs T/P/H = x1, filter off, standby 1000ms, sleep
    dev->settings.osr_t = BME280_OSRS_X1;
    dev->settings.osr_p = BME280_OSRS_X1;
    dev->settings.osr_h = BME280_OSRS_X1;
    dev->settings.filter = BME280_FILTER_OFF;
    dev->settings.standby = BME280_STANDBY_1000_MS;
    dev->settings.mode = BME280_SLEEP_MODE;

    rc = bme280_set_oversampling(dev, dev->settings.osr_t, dev->settings.osr_p, dev->settings.osr_h);
    if (rc != BME280_OK) return rc;
    rc = bme280_set_filter(dev, dev->settings.filter);
    if (rc != BME280_OK) return rc;
    rc = bme280_set_standby(dev, dev->settings.standby);
    if (rc != BME280_OK) return rc;
    rc = bme280_set_mode(dev, BME280_SLEEP_MODE);
    return rc;
}

int bme280_set_oversampling(bme280_t *dev, bme280_oversampling_t osr_t, bme280_oversampling_t osr_p, bme280_oversampling_t osr_h) {
    if (!dev) return BME280_E_NULL_PTR;
    if (osr_t > BME280_OSRS_X16 || osr_p > BME280_OSRS_X16 || osr_h > BME280_OSRS_X16) return BME280_E_INVALID_ARG;

    int rc = bme280_update_bits(dev, BME280_REG_CTRL_HUM, 0x07, (uint8_t)osr_h);
    if (rc != BME280_OK) return rc;

    // ctrl_meas: osrs_t[7:5], osrs_p[4:2], mode[1:0]
    uint8_t ctrl_meas;
    rc = bme280_read_u8(dev, BME280_REG_CTRL_MEAS, &ctrl_meas);
    if (rc != BME280_OK) return rc;
    ctrl_meas = (uint8_t)((ctrl_meas & 0x03) | ((osr_t & 0x07) << 5) | ((osr_p & 0x07) << 2));
    rc = bme280_write_u8(dev, BME280_REG_CTRL_MEAS, ctrl_meas);
    if (rc != BME280_OK) return rc;

    // Writing to ctrl_meas is required after ctrl_hum for changes to take effect
    dev->settings.osr_t = osr_t;
    dev->settings.osr_p = osr_p;
    dev->settings.osr_h = osr_h;
    return BME280_OK;
}

int bme280_set_filter(bme280_t *dev, bme280_filter_t filter) {
    if (!dev) return BME280_E_NULL_PTR;
    if (filter > BME280_FILTER_16) return BME280_E_INVALID_ARG;
    int rc = bme280_update_bits(dev, BME280_REG_CONFIG, 0x1C, (uint8_t)(filter << 2)); // filter[4:2]
    if (rc == BME280_OK) dev->settings.filter = filter;
    return rc;
}

int bme280_set_standby(bme280_t *dev, bme280_standby_t standby) {
    if (!dev) return BME280_E_NULL_PTR;
    if (standby > BME280_STANDBY_20_MS) return BME280_E_INVALID_ARG;
    int rc = bme280_update_bits(dev, BME280_REG_CONFIG, 0xE0, (uint8_t)(standby << 5)); // t_sb[7:5]
    if (rc == BME280_OK) dev->settings.standby = standby;
    return rc;
}

int bme280_set_mode(bme280_t *dev, bme280_mode_t mode) {
    if (!dev) return BME280_E_NULL_PTR;
    if (!(mode == BME280_SLEEP_MODE || mode == BME280_FORCED_MODE || mode == BME280_NORMAL_MODE)) return BME280_E_INVALID_ARG;
    int rc = bme280_update_bits(dev, BME280_REG_CTRL_MEAS, 0x03, (uint8_t)mode);
    if (rc == BME280_OK) dev->settings.mode = mode;
    return rc;
}

int bme280_read_raw(bme280_t *dev, int32_t *adc_T, int32_t *adc_P, int32_t *adc_H) {
    if (!dev) return BME280_E_NULL_PTR;
    uint8_t buf[8];
    int rc = bme280_read_buf(dev, BME280_REG_PRESS_MSB, buf, sizeof(buf));
    if (rc != BME280_OK) return rc;

    int32_t p = ((int32_t)buf[0] << 12) | ((int32_t)buf[1] << 4) | ((int32_t)buf[2] >> 4);
    int32_t t = ((int32_t)buf[3] << 12) | ((int32_t)buf[4] << 4) | ((int32_t)buf[5] >> 4);
    int32_t h = ((int32_t)buf[6] << 8) | buf[7];

    if (adc_P) *adc_P = p;
    if (adc_T) *adc_T = t;
    if (adc_H) *adc_H = h;
    return BME280_OK;
}

float bme280_compensate_temperature(bme280_t *dev, int32_t adc_T) {
    if (!dev || !dev->calib_loaded) return 0.0f;
    // Bosch datasheet compensation formula
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)dev->calib.dig_T1 << 1))) * ((int32_t)dev->calib.dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)dev->calib.dig_T1)) * ((adc_T >> 4) - ((int32_t)dev->calib.dig_T1))) >> 12) * ((int32_t)dev->calib.dig_T3)) >> 14;
    dev->t_fine = var1 + var2;
    float T = (dev->t_fine * 5 + 128) / 256.0f; // in 0.01 C -> C
    return T / 100.0f;
}

float bme280_compensate_pressure(bme280_t *dev, int32_t adc_P) {
    if (!dev || !dev->calib_loaded) return 0.0f;
    int64_t var1 = (int64_t)dev->t_fine - 128000;
    int64_t var2 = var1 * var1 * (int64_t)dev->calib.dig_P6;
    var2 = var2 + ((var1 * (int64_t)dev->calib.dig_P5) << 17);
    var2 = var2 + (((int64_t)dev->calib.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)dev->calib.dig_P3) >> 8) + ((var1 * (int64_t)dev->calib.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)dev->calib.dig_P1) >> 33;

    if (var1 == 0) {
        return 0.0f; // avoid division by zero
    }
    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)dev->calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)dev->calib.dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)dev->calib.dig_P7) << 4);
    return (float)p / 256.0f; // Pa
}

float bme280_compensate_humidity(bme280_t *dev, int32_t adc_H) {
    if (!dev || !dev->calib_loaded) return 0.0f;
    int32_t v_x1_u32r = dev->t_fine - ((int32_t)76800);
    v_x1_u32r = (((((adc_H << 14) - (((int32_t)dev->calib.dig_H4) << 20) - (((int32_t)dev->calib.dig_H5) * v_x1_u32r)) + ((int32_t)16384)) >> 15) *
                 (((((((v_x1_u32r * ((int32_t)dev->calib.dig_H6)) >> 10) * (((v_x1_u32r * ((int32_t)dev->calib.dig_H3)) >> 11) + ((int32_t)32768))) >> 10) +
                     ((int32_t)2097152)) * ((int32_t)dev->calib.dig_H2) + 8192) >> 14));
    v_x1_u32r = v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * ((int32_t)dev->calib.dig_H1)) >> 4);
    v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);
    v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);
    float h = (v_x1_u32r >> 12) / 1024.0f; // %RH
    if (h < 0.0f) h = 0.0f;
    if (h > 100.0f) h = 100.0f;
    return h;
}

int bme280_read_measurement(bme280_t *dev, bme280_reading_t *out) {
    if (!dev || !out) return BME280_E_NULL_PTR;

    if (dev->settings.mode == BME280_FORCED_MODE) {
        int rc = bme280_set_mode(dev, BME280_FORCED_MODE); // trigger one-shot
        if (rc != BME280_OK) return rc;
        // Wait until measuring bit clears
        for (int i = 0; i < 50; ++i) {
            uint8_t st = 0;
            rc = bme280_read_u8(dev, BME280_REG_STATUS, &st);
            if (rc != BME280_OK) return rc;
            if ((st & BME280_STATUS_MEASURING) == 0) break;
            bme280_delay(dev, 5);
        }
    }

    int32_t adc_T = 0, adc_P = 0, adc_H = 0;
    int rc = bme280_read_raw(dev, &adc_T, &adc_P, &adc_H);
    if (rc != BME280_OK) return rc;

    out->temperature_c = bme280_compensate_temperature(dev, adc_T);
    out->pressure_pa   = bme280_compensate_pressure(dev, adc_P);
    out->humidity_rh   = bme280_compensate_humidity(dev, adc_H);
    return BME280_OK;
}

// ===== Sensor.h adapters =====

// Reuse the public wrapper context type to avoid duplicate definitions

static bool bme280__get_event_temp(void *ctx, sensors_event_t *event) {
    if (!ctx || !event) return false;
    bme280_sensor_wrapper_t *c = (bme280_sensor_wrapper_t *)ctx;
    bme280_reading_t r;
    if (bme280_read_measurement(c->dev, &r) != BME280_OK) return false;
    event->version = sizeof(*event);
    event->sensor_id = c->sensor_id;
    event->type = SENSOR_TYPE_AMBIENT_TEMPERATURE;
    event->timestamp = 0; // caller may fill
    event->value.temperature = r.temperature_c;
    return true;
}

static void bme280__get_sensor_temp(void *ctx, sensor_t *sensor) {
    if (!ctx || !sensor) return;
    bme280_sensor_wrapper_t *c = (bme280_sensor_wrapper_t *)ctx;
    (void)c;
    sensor->version = 1;
    sensor->sensor_id = c->sensor_id;
    sensor->type = SENSOR_TYPE_AMBIENT_TEMPERATURE;
    sensor->max_value = 85.0f;
    sensor->min_value = -40.0f;
    sensor->resolution = 0.01f;
    sensor->min_delay = 0;
    sensor->init_delay = 2;
}

static bool bme280__get_event_pressure(void *ctx, sensors_event_t *event) {
    if (!ctx || !event) return false;
    bme280_sensor_wrapper_t *c = (bme280_sensor_wrapper_t *)ctx;
    bme280_reading_t r;
    if (bme280_read_measurement(c->dev, &r) != BME280_OK) return false;
    event->version = sizeof(*event);
    event->sensor_id = c->sensor_id;
    event->type = SENSOR_TYPE_PRESSURE;
    event->timestamp = 0;
    event->value.pressure = r.pressure_pa / 100.0f; // hPa
    return true;
}

static void bme280__get_sensor_pressure(void *ctx, sensor_t *sensor) {
    if (!ctx || !sensor) return;
    bme280_sensor_wrapper_t *c = (bme280_sensor_wrapper_t *)ctx;
    (void)c;
    sensor->version = 1;
    sensor->sensor_id = c->sensor_id;
    sensor->type = SENSOR_TYPE_PRESSURE;
    sensor->max_value = 1100.0f; // hPa
    sensor->min_value = 300.0f;  // hPa
    sensor->resolution = 0.16f;  // typical
    sensor->min_delay = 0;
    sensor->init_delay = 2;
}

static bool bme280__get_event_humidity(void *ctx, sensors_event_t *event) {
    if (!ctx || !event) return false;
    bme280_sensor_wrapper_t *c = (bme280_sensor_wrapper_t *)ctx;
    bme280_reading_t r;
    if (bme280_read_measurement(c->dev, &r) != BME280_OK) return false;
    event->version = sizeof(*event);
    event->sensor_id = c->sensor_id;
    event->type = SENSOR_TYPE_RELATIVE_HUMIDITY;
    event->timestamp = 0;
    event->value.relative_humidity = r.humidity_rh;
    return true;
}

static void bme280__get_sensor_humidity(void *ctx, sensor_t *sensor) {
    if (!ctx || !sensor) return;
    bme280_sensor_wrapper_t *c = (bme280_sensor_wrapper_t *)ctx;
    (void)c;
    sensor->version = 1;
    sensor->sensor_id = c->sensor_id;
    sensor->type = SENSOR_TYPE_RELATIVE_HUMIDITY;
    sensor->max_value = 100.0f;
    sensor->min_value = 0.0f;
    sensor->resolution = 1.0f; // typical
    sensor->min_delay = 0;
    sensor->init_delay = 2;
}

// New instance-safe wrappers as declared in header
void bme280_make_temperature_sensor(bme280_sensor_wrapper_t *ctx, bme280_t *dev, sensor_interface_t *iface, int32_t sensor_id) {
    if (!ctx || !iface) return;
    ctx->dev = dev;
    ctx->sensor_id = sensor_id;
    ctx->type = SENSOR_TYPE_AMBIENT_TEMPERATURE;
    iface->context = ctx;
    iface->get_event = bme280__get_event_temp;
    iface->get_sensor = bme280__get_sensor_temp;
}

void bme280_make_pressure_sensor(bme280_sensor_wrapper_t *ctx, bme280_t *dev, sensor_interface_t *iface, int32_t sensor_id) {
    if (!ctx || !iface) return;
    ctx->dev = dev;
    ctx->sensor_id = sensor_id;
    ctx->type = SENSOR_TYPE_PRESSURE;
    iface->context = ctx;
    iface->get_event = bme280__get_event_pressure;
    iface->get_sensor = bme280__get_sensor_pressure;
}

void bme280_make_humidity_sensor(bme280_sensor_wrapper_t *ctx, bme280_t *dev, sensor_interface_t *iface, int32_t sensor_id) {
    if (!ctx || !iface) return;
    ctx->dev = dev;
    ctx->sensor_id = sensor_id;
    ctx->type = SENSOR_TYPE_RELATIVE_HUMIDITY;
    iface->context = ctx;
    iface->get_event = bme280__get_event_humidity;
    iface->get_sensor = bme280__get_sensor_humidity;
}
