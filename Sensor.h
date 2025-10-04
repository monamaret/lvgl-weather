#ifndef SENSOR_H
#define SENSOR_H

/*
  Portable C version of the Adafruit Unified Sensor interface
  - No Arduino.h dependency
  - Usable from C (C99) on Linux and other POSIX systems
  - Compatible type names and constants to ease porting from Adafruit_Sensor

  Notes:
  - Timestamps use unsigned 64-bit milliseconds since an arbitrary epoch
  - String fields are plain C char arrays
  - Virtual methods are replaced with function pointers (see sensor_interface_t)
*/

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* Common constants (match Adafruit values where appropriate) */
#define SENSORS_GRAVITY_EARTH            (9.80665F)   /* m/s^2 */
#define SENSORS_GRAVITY_MOON             (1.62F)      /* m/s^2 */
#define SENSORS_GRAVITY_SUN              (275.0F)     /* m/s^2 */
#define SENSORS_GRAVITY_STANDARD         (9.80665F)   /* m/s^2 */

#define SENSORS_MAGFIELD_EARTH_MAX       (60.0F)      /* uT */
#define SENSORS_MAGFIELD_EARTH_MIN       (30.0F)      /* uT */

#define SENSORS_PRESSURE_SEALEVELHPA     (1013.25F)   /* hPa */

#define SENSORS_DPS_TO_RADS              (0.017453293F) /* rad/s per dps */
#define SENSORS_RADS_TO_DPS              (57.29577793F) /* dps per rad/s */

/* Sensor type identifiers (aligned with Adafruit/Android where applicable) */
typedef enum {
  SENSOR_TYPE_ACCELEROMETER       = 1,
  SENSOR_TYPE_MAGNETIC_FIELD      = 2,
  SENSOR_TYPE_ORIENTATION         = 3,
  SENSOR_TYPE_GYROSCOPE           = 4,
  SENSOR_TYPE_LIGHT               = 5,
  SENSOR_TYPE_PRESSURE            = 6,
  SENSOR_TYPE_PROXIMITY           = 8,
  SENSOR_TYPE_GRAVITY             = 9,
  SENSOR_TYPE_LINEAR_ACCELERATION = 10,
  SENSOR_TYPE_ROTATION_VECTOR     = 11,
  SENSOR_TYPE_RELATIVE_HUMIDITY   = 12,
  SENSOR_TYPE_AMBIENT_TEMPERATURE = 13,
  SENSOR_TYPE_OBJECT_TEMPERATURE  = 14,
  SENSOR_TYPE_VOLTAGE             = 15,
  SENSOR_TYPE_CURRENT             = 16,
  SENSOR_TYPE_COLOR               = 17
} sensors_type_t;

/* 3-axis sensor vector (accel/gyro/magnetometer/orientation) */
typedef struct {
  float x;
  float y;
  float z;
  int8_t status;  /* -1 = unknown, 0 = unreliable, 1 = low, 2 = medium, 3 = high */
  uint8_t reserved[3]; /* pad to 16 bytes + alignment */
} sensors_vec_t;

/* Color measurement */
typedef struct {
  float r;
  float g;
  float b;
  float c; /* clear */
} sensors_color_t;

/* Unified sensor event */
typedef struct {
  int32_t version;     /* must be set to sizeof(sensors_event_t) */
  int32_t sensor_id;   /* unique sensor identifier */
  int32_t type;        /* sensors_type_t */
  int32_t reserved0;   /* reserved/padding */
  uint64_t timestamp;  /* milliseconds */
  union {
    float           data[4]; /* generic data buffer */
    sensors_vec_t   acceleration;       /* m/s^2 */
    sensors_vec_t   magnetic;           /* uT */
    sensors_vec_t   orientation;        /* degrees */
    sensors_vec_t   gyro;               /* rad/s or dps */
    float           temperature;        /* degrees C */
    float           distance;           /* centimeters */
    float           light;              /* lux */
    float           pressure;           /* hPa */
    float           relative_humidity;  /* % */
    float           current;            /* mA */
    float           voltage;            /* V */
    sensors_color_t color;              /* RGBC */
  } value;
} sensors_event_t;

/* Static sensor metadata */
#ifndef SENSOR_NAME_MAXLEN
#define SENSOR_NAME_MAXLEN 32
#endif

typedef struct {
  char     name[SENSOR_NAME_MAXLEN]; /* short name of this sensor */
  int32_t  version;                  /* version of the hardware + driver */
  int32_t  sensor_id;                /* unique sensor identifier */
  int32_t  type;                     /* sensors_type_t */
  float    max_value;                /* maximum value the sensor can report */
  float    min_value;                /* minimum value the sensor can report */
  float    resolution;               /* smallest difference between two values */
  int32_t  min_delay;                /* min delay between events in microseconds */
  int32_t  init_delay;               /* time to initialize in milliseconds */
} sensor_t;

/* Function-pointer-based interface to replace C++ virtual methods */
typedef struct sensor_interface {
  void *context; /* user-provided context passed to callbacks (may be NULL) */
  bool (*get_event)(void *context, sensors_event_t *event);
  void (*get_sensor)(void *context, sensor_t *sensor);
} sensor_interface_t;

/* Helper inline wrappers for convenience */
static inline bool sensor_get_event(const sensor_interface_t *iface, sensors_event_t *event) {
  if (!iface || !iface->get_event) return false;
  return iface->get_event(iface->context, event);
}

static inline void sensor_get_info(const sensor_interface_t *iface, sensor_t *sensor) {
  if (!iface || !iface->get_sensor) return;
  iface->get_sensor(iface->context, sensor);
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SENSOR_H */
