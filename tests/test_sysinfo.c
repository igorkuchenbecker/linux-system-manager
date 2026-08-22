#include <string.h>

#include "system/sysinfo.h"
#include "test_common.h"

static void test_collect_populates_fields(void)
{
    lsm_system_info_t info;
    /* Any status is acceptable here (a container/CI runner may lack some
     * sources like /etc/os-release); what matters is that every field is
     * left in a well-defined, non-garbage state. */
    lsm_sysinfo_collect(&info);

    TEST_ASSERT(strlen(info.hostname) > 0, "hostname must never be left empty");
    TEST_ASSERT(strlen(info.kernel_name) > 0, "kernel_name must never be left empty");
    TEST_ASSERT(strcmp(info.kernel_name, "Linux") == 0, "kernel_name should be Linux on this platform");
    TEST_ASSERT(strlen(info.architecture) > 0, "architecture must never be left empty");
    TEST_ASSERT(info.cpu_logical_count >= 1, "there must be at least one online logical CPU");
    TEST_ASSERT(info.ram_total_kib > 0, "RAM total should be a positive figure on a real machine");
    TEST_ASSERT(info.uptime_seconds >= 0.0, "uptime should never be negative");
    TEST_ASSERT(strlen(info.cpu_model) > 0, "cpu_model must fall back to a non-empty placeholder");

    TEST_OK("collect_populates_fields");
}

static void test_collect_rejects_null(void)
{
    lsm_status_t status = lsm_sysinfo_collect(NULL);
    TEST_ASSERT(status == LSM_ERR_INVALID_ARG, "collecting into a NULL pointer must be rejected");
    TEST_OK("collect_rejects_null");
}

int main(void)
{
    test_collect_populates_fields();
    test_collect_rejects_null();
    return 0;
}
