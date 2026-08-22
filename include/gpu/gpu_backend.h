#ifndef LSM_GPU_GPU_BACKEND_H
#define LSM_GPU_GPU_BACKEND_H

#include "gpu/gpu.h"

/*
 * Internal interface between gpu.c (the orchestrator) and the per-vendor
 * backend translation units. Not part of the public API — main.c and
 * anything outside src/gpu/ should only ever call gpu.h's
 * lsm_gpu_collect()/lsm_gpu_free().
 */

/* Appends one entry to `list`, growing its backing array as needed.
 * Shared by every backend so the growable-array logic exists once. */
lsm_status_t lsm_gpu_list_append(lsm_gpu_list_t *list, const lsm_gpu_info_t *info);

/*
 * NVIDIA backend: dlopen's libnvidia-ml.so(.1) and, if present, enumerates
 * every GPU NVML reports directly (NVML has its own device enumeration,
 * independent of /sys/class/drm — a GPU bound to the "nvidia" driver is
 * therefore handled exclusively here, not cross-referenced against DRM
 * cards). Appends nothing and returns LSM_OK if the library cannot be
 * loaded (no NVIDIA driver installed) — that is the expected common case
 * on non-NVIDIA machines, not an error.
 */
lsm_status_t lsm_gpu_nvidia_collect(lsm_gpu_list_t *list);

/*
 * AMD backend: for one /sys/class/drm/cardN directory already identified
 * as PCI vendor 0x1002 (AMD) bound to the amdgpu driver, reads VRAM usage
 * (mem_info_vram_total/used) and GPU utilization (gpu_busy_percent) —
 * amdgpu-specific sysfs attributes documented in the kernel's amdgpu
 * driver docs — plus temperature via the card's own hwmon child.
 */
lsm_status_t lsm_gpu_amd_collect(lsm_gpu_list_t *list, const char *card_path,
                                    const char *driver);

/*
 * Intel backend: for one /sys/class/drm/cardN directory identified as
 * PCI vendor 0x8086 (Intel). i915's sysfs surface for frequency/power
 * has changed shape across kernel versions (single-GT vs. multi-tile
 * layouts) and this project will not guess at a path it cannot verify —
 * see docs/TROUBLESHOOTING.md. This backend therefore reports identity
 * (name/driver) only; all has_* metric flags are left at 0.
 */
lsm_status_t lsm_gpu_intel_collect(lsm_gpu_list_t *list, const char *card_path,
                                     const char *driver);

/*
 * Generic fallback backend: any GPU not handled by a vendor-specific
 * backend above (unknown PCI vendor, or a known vendor without a
 * dedicated backend covering this exact driver, e.g. nouveau). Reports
 * identity only, via generic DRM/PCI sysfs attributes common to every
 * device.
 */
lsm_status_t lsm_gpu_generic_collect(lsm_gpu_list_t *list, const char *card_path,
                                       const char *driver, lsm_gpu_vendor_t vendor);

#endif /* LSM_GPU_GPU_BACKEND_H */
