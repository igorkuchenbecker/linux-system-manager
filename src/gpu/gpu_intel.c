#include "gpu/gpu_backend.h"

#include <stdio.h>
#include <string.h>

#include "utils/fileutils.h"
#include "utils/strutils.h"

/*
 * Intel backend — deliberately minimal. i915's sysfs layout for
 * frequency/power (gt_cur_freq_mhz and friends) has moved between a
 * flat .../cardN/ layout and a nested .../cardN/gt/gt0/ layout across
 * kernel versions, and this project will not guess at a path it has not
 * verified against a running kernel (see docs/TROUBLESHOOTING.md and the
 * project's integrity rule against inventing kernel interfaces). Only
 * identity — obtained the same generic-DRM way the fallback backend
 * does — is reported; all metric has_* flags are left at 0 (unsupported)
 * until a verified per-kernel-version sysfs path is added.
 */
static void read_pci_device_id(const char *card_path, char *out, size_t out_size)
{
    char path[320];
    snprintf(path, sizeof(path), "%s/device/device", card_path);

    char buf[16];
    if (lsm_read_file(path, buf, sizeof(buf), NULL) != LSM_OK) {
        lsm_strlcpy(out, "unknown", out_size);
        return;
    }
    lsm_strlcpy(out, lsm_str_trim(buf), out_size);
}

lsm_status_t lsm_gpu_intel_collect(lsm_gpu_list_t *list, const char *card_path,
                                     const char *driver)
{
    lsm_gpu_info_t info;
    memset(&info, 0, sizeof(info));
    info.vendor = LSM_GPU_VENDOR_INTEL;
    lsm_strlcpy(info.driver, driver, sizeof(info.driver));

    char device_id[16];
    read_pci_device_id(card_path, device_id, sizeof(device_id));
    snprintf(info.name, sizeof(info.name), "Intel GPU (PCI ID 8086:%s)", device_id);

    return lsm_gpu_list_append(list, &info);
}
