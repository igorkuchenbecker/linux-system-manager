#include <string.h>

#include "gpu/gpu.h"
#include "test_common.h"

static void test_collect_degrades_gracefully(void)
{
    lsm_gpu_list_t list = {0};
    lsm_status_t status = lsm_gpu_collect(&list);
    TEST_ASSERT(status == LSM_OK, "GPU collection must always return LSM_OK, even with 0 GPUs");

    for (size_t i = 0; i < list.count; i++) {
        const lsm_gpu_info_t *gpu = &list.items[i];
        TEST_ASSERT(strlen(gpu->name) > 0, "every reported GPU must have a non-empty name");
        TEST_ASSERT(gpu->vendor == LSM_GPU_VENDOR_NVIDIA || gpu->vendor == LSM_GPU_VENDOR_AMD ||
                    gpu->vendor == LSM_GPU_VENDOR_INTEL || gpu->vendor == LSM_GPU_VENDOR_UNKNOWN,
                    "vendor must be one of the known enum values");
        if (gpu->has_memory) {
            TEST_ASSERT(gpu->memory_used_kib <= gpu->memory_total_kib,
                        "reported used VRAM cannot exceed total VRAM");
        }
        if (gpu->has_temperature) {
            TEST_ASSERT(gpu->temperature_c > -50.0 && gpu->temperature_c < 200.0,
                        "a reported GPU temperature must be within a physically plausible range");
        }
        if (gpu->has_utilization_percent) {
            TEST_ASSERT(gpu->utilization_percent >= 0.0 && gpu->utilization_percent <= 100.0,
                        "utilization percentage must be within 0-100");
        }
    }

    lsm_gpu_free(&list);
    TEST_ASSERT(list.items == NULL && list.count == 0,
                "lsm_gpu_free must reset the struct to empty");
    TEST_OK("collect_degrades_gracefully");
}

static void test_collect_rejects_null(void)
{
    lsm_status_t status = lsm_gpu_collect(NULL);
    TEST_ASSERT(status == LSM_ERR_INVALID_ARG, "NULL list pointer must be rejected");
    TEST_OK("collect_rejects_null");
}

int main(void)
{
    test_collect_degrades_gracefully();
    test_collect_rejects_null();
    return 0;
}
