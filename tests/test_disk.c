#include <string.h>

#include "disk/disk.h"
#include "test_common.h"

static void test_mounts_collect_finds_root(void)
{
    lsm_mount_list_t list = {0};
    lsm_status_t status = lsm_disk_mounts_collect(&list);
    TEST_ASSERT(status == LSM_OK, "/proc/mounts should always be readable on Linux");
    TEST_ASSERT(list.count >= 1, "there must be at least one real mount point");

    int found_root = 0;
    for (size_t i = 0; i < list.count; i++) {
        const lsm_mount_info_t *m = &list.items[i];
        TEST_ASSERT(m->available_kib <= m->total_kib || m->total_kib == 0,
                    "available space should not exceed total space");
        if (strcmp(m->mount_point, "/") == 0) {
            found_root = 1;
            TEST_ASSERT(m->total_kib > 0, "root filesystem must report nonzero capacity");
        }
    }
    TEST_ASSERT(found_root, "root mount point '/' must be present");

    lsm_disk_mounts_free(&list);
    TEST_ASSERT(list.items == NULL && list.count == 0,
                "lsm_disk_mounts_free must reset the struct to empty");
    TEST_OK("mounts_collect_finds_root");
}

static void test_mounts_collect_rejects_null(void)
{
    lsm_status_t status = lsm_disk_mounts_collect(NULL);
    TEST_ASSERT(status == LSM_ERR_INVALID_ARG, "NULL list pointer must be rejected");
    TEST_OK("mounts_collect_rejects_null");
}

static void test_devices_collect_degrades_gracefully(void)
{
    lsm_disk_device_list_t list = {0};
    lsm_status_t status = lsm_disk_devices_collect(&list);
    /* /sys/block may be absent in some minimal containers -> unsupported
     * is an acceptable outcome; anything else must be LSM_OK. */
    TEST_ASSERT(status == LSM_OK || status == LSM_ERR_UNSUPPORTED,
                "device enumeration must either succeed or report unsupported");

    if (status == LSM_OK) {
        for (size_t i = 0; i < list.count; i++) {
            TEST_ASSERT(strlen(list.items[i].name) > 0, "device name must not be empty");
            TEST_ASSERT(strlen(list.items[i].model) > 0,
                        "model must fall back to a non-empty placeholder");
        }
    }

    lsm_disk_devices_free(&list);
    TEST_ASSERT(list.items == NULL && list.count == 0,
                "lsm_disk_devices_free must reset the struct to empty");
    TEST_OK("devices_collect_degrades_gracefully");
}

static void test_devices_collect_rejects_null(void)
{
    lsm_status_t status = lsm_disk_devices_collect(NULL);
    TEST_ASSERT(status == LSM_ERR_INVALID_ARG, "NULL list pointer must be rejected");
    TEST_OK("devices_collect_rejects_null");
}

int main(void)
{
    test_mounts_collect_finds_root();
    test_mounts_collect_rejects_null();
    test_devices_collect_degrades_gracefully();
    test_devices_collect_rejects_null();
    return 0;
}
