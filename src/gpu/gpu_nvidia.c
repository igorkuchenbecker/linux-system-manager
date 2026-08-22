#include "gpu/gpu_backend.h"

#include <dlfcn.h>
#include <string.h>

#include "utils/strutils.h"

/*
 * Minimal NVML surface used here. These signatures/structs are part of
 * NVML's long-stable public ABI (unchanged since early CUDA toolkit
 * releases, since NVIDIA guarantees NVML backward compatibility) and
 * were cross-checked against the symbol table of the installed
 * libnvidia-ml.so.1 during development (nm -D). Declared locally instead
 * of including <nvml.h> so this project has no build-time dependency on
 * the CUDA toolkit being installed — everything is resolved at runtime
 * via dlopen/dlsym, and absence of the library is a normal, expected,
 * gracefully-handled outcome on any non-NVIDIA machine.
 */
typedef struct { unsigned long long total, free, used; } lsm_nvml_memory_t;
typedef struct { unsigned int gpu, memory; } lsm_nvml_utilization_t;

typedef int (*nvml_init_fn)(void);
typedef int (*nvml_shutdown_fn)(void);
typedef int (*nvml_device_get_count_fn)(unsigned int *count);
typedef int (*nvml_device_get_handle_fn)(unsigned int index, void **device);
typedef int (*nvml_device_get_name_fn)(void *device, char *name, unsigned int length);
typedef int (*nvml_device_get_temperature_fn)(void *device, int sensor_type, unsigned int *temp);
typedef int (*nvml_device_get_memory_info_fn)(void *device, lsm_nvml_memory_t *memory);
typedef int (*nvml_device_get_utilization_fn)(void *device, lsm_nvml_utilization_t *util);

#define NVML_SUCCESS 0
#define NVML_TEMPERATURE_GPU 0

lsm_status_t lsm_gpu_nvidia_collect(lsm_gpu_list_t *list)
{
    void *handle = dlopen("libnvidia-ml.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (handle == NULL)
        handle = dlopen("libnvidia-ml.so", RTLD_LAZY | RTLD_LOCAL);
    if (handle == NULL)
        return LSM_OK; /* no NVIDIA driver/userspace library installed */

    nvml_init_fn nvml_init = (nvml_init_fn)dlsym(handle, "nvmlInit_v2");
    nvml_shutdown_fn nvml_shutdown = (nvml_shutdown_fn)dlsym(handle, "nvmlShutdown");
    nvml_device_get_count_fn get_count =
        (nvml_device_get_count_fn)dlsym(handle, "nvmlDeviceGetCount_v2");
    nvml_device_get_handle_fn get_handle =
        (nvml_device_get_handle_fn)dlsym(handle, "nvmlDeviceGetHandleByIndex_v2");
    nvml_device_get_name_fn get_name = (nvml_device_get_name_fn)dlsym(handle, "nvmlDeviceGetName");
    nvml_device_get_temperature_fn get_temp =
        (nvml_device_get_temperature_fn)dlsym(handle, "nvmlDeviceGetTemperature");
    nvml_device_get_memory_info_fn get_memory =
        (nvml_device_get_memory_info_fn)dlsym(handle, "nvmlDeviceGetMemoryInfo");
    nvml_device_get_utilization_fn get_util =
        (nvml_device_get_utilization_fn)dlsym(handle, "nvmlDeviceGetUtilizationRates");

    if (!nvml_init || !nvml_shutdown || !get_count || !get_handle || !get_name ||
        !get_temp || !get_memory || !get_util) {
        /* Library present but missing an expected symbol — an NVML
         * version too old/new to trust. Degrade rather than risk calling
         * through a mismatched signature. */
        dlclose(handle);
        return LSM_OK;
    }

    if (nvml_init() != NVML_SUCCESS) {
        dlclose(handle);
        return LSM_OK;
    }

    lsm_status_t result = LSM_OK;
    unsigned int count = 0;
    if (get_count(&count) == NVML_SUCCESS) {
        for (unsigned int i = 0; i < count; i++) {
            void *device = NULL;
            if (get_handle(i, &device) != NVML_SUCCESS)
                continue;

            lsm_gpu_info_t info;
            memset(&info, 0, sizeof(info));
            info.vendor = LSM_GPU_VENDOR_NVIDIA;
            lsm_strlcpy(info.driver, "nvidia", sizeof(info.driver));

            char name_buf[96] = {0};
            if (get_name(device, name_buf, sizeof(name_buf)) == NVML_SUCCESS)
                lsm_strlcpy(info.name, name_buf, sizeof(info.name));
            else
                lsm_strlcpy(info.name, "NVIDIA GPU", sizeof(info.name));

            unsigned int temp = 0;
            if (get_temp(device, NVML_TEMPERATURE_GPU, &temp) == NVML_SUCCESS) {
                info.has_temperature = 1;
                info.temperature_c = (double)temp;
            }

            lsm_nvml_utilization_t util;
            memset(&util, 0, sizeof(util));
            if (get_util(device, &util) == NVML_SUCCESS) {
                info.has_utilization_percent = 1;
                info.utilization_percent = (double)util.gpu;
            }

            lsm_nvml_memory_t mem;
            memset(&mem, 0, sizeof(mem));
            if (get_memory(device, &mem) == NVML_SUCCESS) {
                info.has_memory = 1;
                info.memory_total_kib = mem.total / 1024;
                info.memory_used_kib = mem.used / 1024;
            }

            if (lsm_gpu_list_append(list, &info) != LSM_OK) {
                result = LSM_ERR_NOMEM;
                break;
            }
        }
    }

    nvml_shutdown();
    dlclose(handle);
    return result;
}
