#include "gpu/gpu.h"
#include "gpu/gpu_backend.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utils/fileutils.h"
#include "utils/strutils.h"

#define LSM_NVIDIA_VENDOR_ID 0x10de
#define LSM_AMD_VENDOR_ID 0x1002
#define LSM_INTEL_VENDOR_ID 0x8086

lsm_status_t lsm_gpu_list_append(lsm_gpu_list_t *list, const lsm_gpu_info_t *info)
{
    if (list->count == list->capacity) {
        size_t new_capacity = (list->capacity == 0) ? 4 : list->capacity * 2;
        lsm_gpu_info_t *grown = realloc(list->items, new_capacity * sizeof(*grown));
        if (grown == NULL)
            return LSM_ERR_NOMEM;
        list->items = grown;
        list->capacity = new_capacity;
    }
    list->items[list->count++] = *info;
    return LSM_OK;
}

/* A /sys/class/drm entry is a real GPU card ("card0", "card1", ...) only
 * if it is exactly "card" followed by digits — connector pseudo-entries
 * like "card0-DP-1" share the prefix but represent a display output, not
 * a device, and must be excluded. */
static int is_card_directory(const char *name)
{
    if (!lsm_str_has_prefix(name, "card"))
        return 0;
    const char *p = name + 4;
    if (*p == '\0')
        return 0;
    for (; *p != '\0'; p++) {
        if (!isdigit((unsigned char)*p))
            return 0;
    }
    return 1;
}

static unsigned int read_pci_vendor(const char *card_path)
{
    char path[320];
    snprintf(path, sizeof(path), "%s/device/vendor", card_path);

    char buf[16];
    if (lsm_read_file(path, buf, sizeof(buf), NULL) != LSM_OK)
        return 0;

    return (unsigned int)strtoul(buf, NULL, 0); /* base 0: honors the "0x" prefix */
}

/* Resolves the kernel driver bound to this device via the "driver"
 * symlink under .../device/ (e.g. .../device/driver -> .../drivers/nvidia),
 * whose basename is the driver name. Absent for an unbound device. */
static void read_bound_driver(const char *card_path, char *out, size_t out_size)
{
    char path[320];
    snprintf(path, sizeof(path), "%s/device/driver", card_path);

    char target[320];
    ssize_t n = readlink(path, target, sizeof(target) - 1);
    if (n <= 0) {
        out[0] = '\0';
        return;
    }
    target[n] = '\0';

    const char *base = strrchr(target, '/');
    base = (base != NULL) ? base + 1 : target;
    lsm_strlcpy(out, base, out_size);
}

lsm_status_t lsm_gpu_collect(lsm_gpu_list_t *list)
{
    if (list == NULL)
        return LSM_ERR_INVALID_ARG;

    lsm_gpu_free(list);

    /* NVML enumerates its own devices independently of /sys/class/drm —
     * every NVIDIA GPU bound to the proprietary "nvidia" driver is
     * handled exclusively through it, so it runs first and unconditionally. */
    lsm_status_t status = lsm_gpu_nvidia_collect(list);
    if (status == LSM_ERR_NOMEM) {
        lsm_gpu_free(list);
        return status;
    }

    DIR *dir = opendir("/sys/class/drm");
    if (dir == NULL)
        return LSM_OK; /* no DRM subsystem at all: headless/minimal system,
                         * not an error for this collector */

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!is_card_directory(entry->d_name))
            continue;

        char card_path[288];
        snprintf(card_path, sizeof(card_path), "/sys/class/drm/%s", entry->d_name);

        unsigned int vendor_id = read_pci_vendor(card_path);
        char driver[32];
        read_bound_driver(card_path, driver, sizeof(driver));

        if (vendor_id == LSM_NVIDIA_VENDOR_ID) {
            /* Already fully handled by NVML above (whether or not NVML
             * actually succeeded) — a proprietary-driver NVIDIA card has
             * essentially nothing useful in generic DRM sysfs, so a
             * fallback here would only produce an empty-looking
             * duplicate entry, not new information. */
            continue;
        }

        if (vendor_id == LSM_AMD_VENDOR_ID) {
            status = lsm_gpu_amd_collect(list, card_path, driver);
        } else if (vendor_id == LSM_INTEL_VENDOR_ID) {
            status = lsm_gpu_intel_collect(list, card_path, driver);
        } else {
            lsm_gpu_vendor_t vendor = LSM_GPU_VENDOR_UNKNOWN;
            status = lsm_gpu_generic_collect(list, card_path, driver, vendor);
        }

        if (status == LSM_ERR_NOMEM) {
            closedir(dir);
            lsm_gpu_free(list);
            return status;
        }
    }

    closedir(dir);
    return LSM_OK;
}

void lsm_gpu_free(lsm_gpu_list_t *list)
{
    if (list == NULL)
        return;
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}
