#include <string.h>

#include "network/network.h"
#include "test_common.h"

static void test_collect_finds_loopback(void)
{
    lsm_network_list_t list = {0};
    lsm_status_t status = lsm_network_collect(&list);
    TEST_ASSERT(status == LSM_OK, "/proc/net/dev should always be readable on Linux");
    TEST_ASSERT(list.count >= 1, "there must be at least the loopback interface");

    int found_lo = 0;
    for (size_t i = 0; i < list.count; i++) {
        const lsm_network_interface_t *iface = &list.items[i];
        TEST_ASSERT(strlen(iface->name) > 0, "interface name must not be empty");
        TEST_ASSERT(strlen(iface->state) > 0, "state must fall back to a non-empty placeholder");
        if (strcmp(iface->name, "lo") == 0) {
            found_lo = 1;
            TEST_ASSERT(iface->is_loopback, "'lo' must be flagged as loopback");
        }
    }
    TEST_ASSERT(found_lo, "the loopback interface must always be present on Linux");

    lsm_network_free(&list);
    TEST_ASSERT(list.items == NULL && list.count == 0,
                "lsm_network_free must reset the struct to empty");
    TEST_OK("collect_finds_loopback");
}

static void test_collect_rejects_null(void)
{
    lsm_status_t status = lsm_network_collect(NULL);
    TEST_ASSERT(status == LSM_ERR_INVALID_ARG, "NULL list pointer must be rejected");
    TEST_OK("collect_rejects_null");
}

int main(void)
{
    test_collect_finds_loopback();
    test_collect_rejects_null();
    return 0;
}
