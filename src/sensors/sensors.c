#include "sensors/sensors.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/log.h"
#include "utils/fileutils.h"
#include "utils/strutils.h"

#define LSM_TAG "sensors"

const char *lsm_sensor_type_unit(lsm_sensor_type_t type)
{
    switch (type) {
    case LSM_SENSOR_TEMPERATURE: return "\xC2\xB0" "C"; /* UTF-8 for U+00B0 DEGREE SIGN + 'C' */
    case LSM_SENSOR_FAN:         return "RPM";
    case LSM_SENSOR_VOLTAGE:     return "V";
    case LSM_SENSOR_POWER:       return "W";
    }
    return "?";
}

static lsm_status_t ensure_capacity(lsm_sensor_list_t *list)
{
    if (list->count < list->capacity)
        return LSM_OK;

    size_t new_capacity = (list->capacity == 0) ? 32 : list->capacity * 2;
    lsm_sensor_reading_t *grown = realloc(list->items, new_capacity * sizeof(*grown));
    if (grown == NULL)
        return LSM_ERR_NOMEM;

    list->items = grown;
    list->capacity = new_capacity;
    return LSM_OK;
}

/*
 * Matches "<prefix><digits>_input" exactly (e.g. "temp1_input",
 * "in0_input"), rejecting look-alikes such as "intrusion0_alarm" (prefix
 * "in" but no digit immediately follows) or "temp1_crit" (right prefix
 * and digits, wrong suffix). On match, writes the parsed index and
 * returns 1; returns 0 otherwise.
 */
static int match_input_file(const char *name, const char *prefix, int *out_index)
{
    size_t prefix_len = strlen(prefix);
    if (strncmp(name, prefix, prefix_len) != 0)
        return 0;

    const char *digits_start = name + prefix_len;
    const char *p = digits_start;
    while (isdigit((unsigned char)*p))
        p++;
    if (p == digits_start)
        return 0; /* no digits between prefix and suffix */
    if (strcmp(p, "_input") != 0)
        return 0;

    *out_index = atoi(digits_start);
    return 1;
}

static void read_chip_name(const char *hwmon_path, const char *dir_name,
                             char *out, size_t out_size)
{
    char path[320];
    snprintf(path, sizeof(path), "%s/name", hwmon_path);

    char buf[64];
    lsm_status_t status = lsm_read_file(path, buf, sizeof(buf), NULL);
    if (status != LSM_OK) {
        /* Every real hwmon driver provides "name"; a missing one usually
         * means a device mid-teardown. Fall back to the directory name
         * itself so the reading is still attributable to *something*. */
        lsm_strlcpy(out, dir_name, out_size);
        return;
    }
    lsm_strlcpy(out, lsm_str_trim(buf), out_size);
}

static void read_label(const char *hwmon_path, const char *type_prefix, int index,
                         char *out, size_t out_size)
{
    char path[320];
    snprintf(path, sizeof(path), "%s/%s%d_label", hwmon_path, type_prefix, index);

    char buf[64];
    lsm_status_t status = lsm_read_file(path, buf, sizeof(buf), NULL);
    if (status != LSM_OK) {
        /* Most drivers (notably acpitz, many nvme) provide no label at
         * all — this is the common case, not an error. */
        snprintf(out, out_size, "%s%d", type_prefix, index);
        return;
    }
    lsm_strlcpy(out, lsm_str_trim(buf), out_size);
}

static lsm_status_t add_reading(lsm_sensor_list_t *list, const char *hwmon_path,
                                  const char *chip, const char *type_prefix, int index,
                                  lsm_sensor_type_t type, double scale)
{
    char path[320];
    snprintf(path, sizeof(path), "%s/%s%d_input", hwmon_path, type_prefix, index);

    long raw = 0;
    lsm_status_t status = lsm_read_file_long(path, &raw);
    if (status != LSM_OK) {
        /* A sensor can legitimately go unreadable transiently (e.g. a
         * thermal zone mid-suspend); skip this one reading, not the
         * whole device. */
        return LSM_OK;
    }

    if (ensure_capacity(list) != LSM_OK)
        return LSM_ERR_NOMEM;

    lsm_sensor_reading_t *entry = &list->items[list->count];
    lsm_strlcpy(entry->chip, chip, sizeof(entry->chip));
    read_label(hwmon_path, type_prefix, index, entry->label, sizeof(entry->label));
    entry->type = type;
    entry->value = (double)raw * scale;
    list->count++;
    return LSM_OK;
}

static lsm_status_t collect_hwmon_device(lsm_sensor_list_t *list, const char *hwmon_path,
                                           const char *dir_name)
{
    char chip[64];
    read_chip_name(hwmon_path, dir_name, chip, sizeof(chip));

    DIR *dir = opendir(hwmon_path);
    if (dir == NULL)
        return LSM_OK; /* device vanished (e.g. hot-unplugged) between the
                         * outer readdir() and this open() — skip it */

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        int index = 0;
        lsm_status_t status = LSM_OK;

        if (match_input_file(entry->d_name, "temp", &index))
            status = add_reading(list, hwmon_path, chip, "temp", index,
                                  LSM_SENSOR_TEMPERATURE, 1.0 / 1000.0);
        else if (match_input_file(entry->d_name, "fan", &index))
            status = add_reading(list, hwmon_path, chip, "fan", index,
                                  LSM_SENSOR_FAN, 1.0);
        else if (match_input_file(entry->d_name, "in", &index))
            status = add_reading(list, hwmon_path, chip, "in", index,
                                  LSM_SENSOR_VOLTAGE, 1.0 / 1000.0);
        else if (match_input_file(entry->d_name, "power", &index))
            status = add_reading(list, hwmon_path, chip, "power", index,
                                  LSM_SENSOR_POWER, 1.0 / 1000000.0);
        else
            continue;

        if (status == LSM_ERR_NOMEM) {
            closedir(dir);
            return LSM_ERR_NOMEM;
        }
    }

    closedir(dir);
    return LSM_OK;
}

lsm_status_t lsm_sensors_collect(lsm_sensor_list_t *list)
{
    if (list == NULL)
        return LSM_ERR_INVALID_ARG;

    lsm_sensors_free(list);

    DIR *dir = opendir("/sys/class/hwmon");
    if (dir == NULL) {
        if (errno == ENOENT)
            return LSM_ERR_UNSUPPORTED;
        lsm_log_errno(LSM_TAG, "/sys/class/hwmon", errno);
        return LSM_ERR_IO;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        char hwmon_path[288];
        snprintf(hwmon_path, sizeof(hwmon_path), "/sys/class/hwmon/%s", entry->d_name);

        lsm_status_t status = collect_hwmon_device(list, hwmon_path, entry->d_name);
        if (status == LSM_ERR_NOMEM) {
            closedir(dir);
            lsm_sensors_free(list);
            return LSM_ERR_NOMEM;
        }
    }

    closedir(dir);
    return LSM_OK;
}

void lsm_sensors_free(lsm_sensor_list_t *list)
{
    if (list == NULL)
        return;
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}
