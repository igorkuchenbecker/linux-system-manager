#include "memory/memory.h"
#include "test_common.h"

static void test_collect_populates_fields(void)
{
    lsm_memory_info_t info;
    lsm_status_t status = lsm_memory_collect(&info);
    TEST_ASSERT(status == LSM_OK, "/proc/meminfo should always be fully parseable on Linux");

    TEST_ASSERT(info.total_kib > 0, "total RAM must be positive on a real machine");
    TEST_ASSERT(info.available_kib <= info.total_kib, "available memory cannot exceed total");
    TEST_ASSERT(info.used_kib <= info.total_kib, "used memory cannot exceed total");
    TEST_ASSERT(info.used_kib == info.total_kib - info.available_kib,
                "used must be defined as total - available, not total - free");

    if (info.swap_total_kib > 0) {
        TEST_ASSERT(info.swap_used_kib <= info.swap_total_kib,
                    "swap used cannot exceed swap total");
    } else {
        TEST_ASSERT(info.swap_used_kib == 0, "with no swap configured, swap used must be 0");
    }

    TEST_OK("collect_populates_fields");
}

static void test_collect_rejects_null(void)
{
    lsm_status_t status = lsm_memory_collect(NULL);
    TEST_ASSERT(status == LSM_ERR_INVALID_ARG, "NULL output pointer must be rejected");
    TEST_OK("collect_rejects_null");
}

int main(void)
{
    test_collect_populates_fields();
    test_collect_rejects_null();
    return 0;
}
