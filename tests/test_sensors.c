#include <string.h>

#include "sensors/sensors.h"
#include "test_common.h"

static void test_collect_degrades_gracefully(void)
{
    lsm_sensor_list_t list = {0};
    lsm_status_t status = lsm_sensors_collect(&list);
    /* A VM/container without /sys/class/hwmon, or a real machine with
     * zero exposed sensors, are both legitimate outcomes — only a hard
     * I/O error or crash would indicate a bug. */
    TEST_ASSERT(status == LSM_OK || status == LSM_ERR_UNSUPPORTED,
                "sensor collection must either succeed or report unsupported");

    if (status == LSM_OK) {
        for (size_t i = 0; i < list.count; i++) {
            const lsm_sensor_reading_t *r = &list.items[i];
            TEST_ASSERT(strlen(r->chip) > 0, "chip name must not be empty");
            TEST_ASSERT(strlen(r->label) > 0, "label must fall back to a synthesized name");
            if (r->type == LSM_SENSOR_FAN) {
                TEST_ASSERT(r->value >= 0.0, "fan RPM must not be negative");
            }
        }
    }

    lsm_sensors_free(&list);
    TEST_ASSERT(list.items == NULL && list.count == 0,
                "lsm_sensors_free must reset the struct to empty");
    TEST_OK("collect_degrades_gracefully");
}

static void test_collect_rejects_null(void)
{
    lsm_status_t status = lsm_sensors_collect(NULL);
    TEST_ASSERT(status == LSM_ERR_INVALID_ARG, "NULL list pointer must be rejected");
    TEST_OK("collect_rejects_null");
}

static void test_type_unit_strings(void)
{
    TEST_ASSERT(strcmp(lsm_sensor_type_unit(LSM_SENSOR_FAN), "RPM") == 0,
                "fan unit must be RPM");
    TEST_ASSERT(strcmp(lsm_sensor_type_unit(LSM_SENSOR_VOLTAGE), "V") == 0,
                "voltage unit must be V");
    TEST_ASSERT(strcmp(lsm_sensor_type_unit(LSM_SENSOR_POWER), "W") == 0,
                "power unit must be W");
    TEST_ASSERT(strlen(lsm_sensor_type_unit(LSM_SENSOR_TEMPERATURE)) > 0,
                "temperature unit must be a non-empty string");
    TEST_OK("type_unit_strings");
}

int main(void)
{
    test_collect_degrades_gracefully();
    test_collect_rejects_null();
    test_type_unit_strings();
    return 0;
}
