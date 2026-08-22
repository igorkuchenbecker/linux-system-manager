#ifndef LSM_GPU_GPU_H
#define LSM_GPU_GPU_H

#include <stddef.h>
#include <stdint.h>

#include "core/status.h"

/*
 * Phase 8 — GPU.
 *
 * There is no single Linux kernel interface for "GPU metrics" the way
 * /proc/meminfo exists for memory: NVIDIA's proprietary driver exposes
 * almost nothing useful through sysfs and requires NVML (a userspace
 * shared library, dlopen'd here so this project has no hard link-time
 * dependency on it); AMD's amdgpu driver exposes a rich, stable set of
 * sysfs attributes; Intel's i915 driver's sysfs surface is comparatively
 * minimal and has shifted across kernel versions. Each vendor backend
 * (src/gpu/gpu_nvidia.c, gpu_amd.c, gpu_intel.c, gpu_generic.c)
 * therefore reports only the fields it can actually obtain — see the
 * has_* flags below. This is a deliberate rejection of a "fake"
 * abstraction that would show 0/N/A indistinguishably for "the value is
 * genuinely zero" vs. "this backend cannot read this field at all".
 */
typedef enum lsm_gpu_vendor {
    LSM_GPU_VENDOR_NVIDIA,
    LSM_GPU_VENDOR_AMD,
    LSM_GPU_VENDOR_INTEL,
    LSM_GPU_VENDOR_UNKNOWN,
} lsm_gpu_vendor_t;

typedef struct lsm_gpu_info {
    char name[128];             /* e.g. "NVIDIA GeForce RTX 2060"; PCI-ID based
                                  * placeholder if the backend cannot query a name */
    char driver[32];             /* kernel driver bound to the device: "nvidia",
                                   * "nouveau", "amdgpu", "i915", or "" if unknown */
    lsm_gpu_vendor_t vendor;

    int has_temperature;
    double temperature_c;

    int has_utilization_percent;
    double utilization_percent;

    int has_memory;
    uint64_t memory_total_kib;
    uint64_t memory_used_kib;
} lsm_gpu_info_t;

typedef struct lsm_gpu_list {
    lsm_gpu_info_t *items; /* heap-allocated, owned by this struct */
    size_t count;
    size_t capacity;
} lsm_gpu_list_t;

/*
 * Enumerates GPUs visible to this system and fills in whatever each
 * vendor backend can obtain. `list` must be zero-initialized or
 * previously freed. Always returns LSM_OK even if `list->count` ends up
 * 0 (e.g. a headless server with no GPU, or a GPU whose driver exposes
 * nothing this project reads) — the *absence* of GPUs is not itself an
 * error condition for this collector.
 */
lsm_status_t lsm_gpu_collect(lsm_gpu_list_t *list);

/* Releases the array owned by `list` and zeroes it. Safe to call twice. */
void lsm_gpu_free(lsm_gpu_list_t *list);

#endif /* LSM_GPU_GPU_H */
