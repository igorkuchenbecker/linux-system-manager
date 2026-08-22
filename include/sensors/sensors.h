#ifndef LSM_SENSORS_SENSORS_H
#define LSM_SENSORS_SENSORS_H

#include <stddef.h>

#include "core/status.h"

/*
 * Phase 7 — Hardware Sensors.
 *
 * Source: /sys/class/hwmon/hwmon<N>/, the kernel's unified sensor
 * interface (used by lm-sensors itself, among others). Real machines
 * expose wildly different sensor sets — a desktop might have
 * k10temp/nct6775/nvme/amdgpu; a laptop might have only acpitz and one
 * nvme; a VM might have none at all. This module makes no assumption
 * about which sensors exist: it enumerates whatever hwmon exposes and
 * reports nothing for what it does not find (an empty list is a valid,
 * successful result — not an error).
 *
 * hwmon file naming convention (Documentation/hwmon/sysfs-interface.rst):
 * `<type><index>_input` holds the current reading in a fixed kernel unit
 * (millidegree C for temp, RPM as a plain integer for fan, millivolts for
 * "in" (voltage), microwatts for power); an optional sibling
 * `<type><index>_label` holds a human-readable name (e.g. "Tctl",
 * "Composite", "CPU Fan"). This module converts to conventional display
 * units (°C, RPM, V, W) so callers never see raw kernel millis/micros.
 */
typedef enum lsm_sensor_type {
    LSM_SENSOR_TEMPERATURE, /* °C */
    LSM_SENSOR_FAN,          /* RPM */
    LSM_SENSOR_VOLTAGE,      /* V */
    LSM_SENSOR_POWER,        /* W */
} lsm_sensor_type_t;

typedef struct lsm_sensor_reading {
    char chip[64];       /* hwmon "name" file, e.g. "k10temp", "nvme", "amdgpu";
                           * falls back to the hwmonN directory name if unreadable */
    char label[64];       /* "<type>_label" if present, else synthesized "temp1" etc. */
    lsm_sensor_type_t type;
    double value;          /* in the unit implied by `type` (°C/RPM/V/W) */
} lsm_sensor_reading_t;

typedef struct lsm_sensor_list {
    lsm_sensor_reading_t *items; /* heap-allocated, owned by this struct */
    size_t count;
    size_t capacity;
} lsm_sensor_list_t;

/* Human-readable unit suffix for a sensor type ("°C", "RPM", "V", "W"). */
const char *lsm_sensor_type_unit(lsm_sensor_type_t type);

/*
 * Enumerates every temp/fan/in/power reading across every hwmon device.
 * `list` must be zero-initialized or previously freed. A missing
 * /sys/class/hwmon (extremely unlikely on a real Linux system, but
 * possible in a minimal container) is reported as LSM_ERR_UNSUPPORTED,
 * not a hard failure; an individual unreadable sensor file within an
 * otherwise-working hwmon device is skipped, not fatal.
 */
lsm_status_t lsm_sensors_collect(lsm_sensor_list_t *list);

/* Releases the array owned by `list` and zeroes it. Safe to call twice. */
void lsm_sensors_free(lsm_sensor_list_t *list);

#endif /* LSM_SENSORS_SENSORS_H */
