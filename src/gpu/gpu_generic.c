#include "gpu/gpu_backend.h"

#include <stdio.h>
#include <string.h>

#include "utils/fileutils.h"
#include "utils/strutils.h"

/*
 * Fallback backend for anything not covered by a dedicated vendor
 * backend: unrecognized PCI vendors, or a known vendor bound to a driver
 * this project has no specific support for (e.g. an NVIDIA card running
 * the open-source nouveau driver instead of the proprietary one — NVML
 * only talks to the proprietary driver, and nouveau has no comparably
 * rich sysfs metrics interface). Reports identity via the vendor/device
 * PCI IDs, which every DRM device exposes uniformly regardless of driver.
 */
lsm_status_t lsm_gpu_generic_collect(lsm_gpu_list_t *list, const char *card_path,
                                       const char *driver, lsm_gpu_vendor_t vendor)
{
    lsm_gpu_info_t info;
    memset(&info, 0, sizeof(info));
    info.vendor = vendor;
    lsm_strlcpy(info.driver, driver, sizeof(info.driver));

    char vendor_id[16], device_id[16];
    char path[320];

    snprintf(path, sizeof(path), "%s/device/vendor", card_path);
    if (lsm_read_file(path, vendor_id, sizeof(vendor_id), NULL) != LSM_OK)
        lsm_strlcpy(vendor_id, "unknown", sizeof(vendor_id));
    else
        lsm_str_trim(vendor_id);

    snprintf(path, sizeof(path), "%s/device/device", card_path);
    if (lsm_read_file(path, device_id, sizeof(device_id), NULL) != LSM_OK)
        lsm_strlcpy(device_id, "unknown", sizeof(device_id));
    else
        lsm_str_trim(device_id);

    snprintf(info.name, sizeof(info.name), "GPU (PCI ID %s:%s, driver: %s)",
             vendor_id, device_id, driver[0] != '\0' ? driver : "none");

    return lsm_gpu_list_append(list, &info);
}
