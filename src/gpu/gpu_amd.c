#include "gpu/gpu_backend.h"

#include <dirent.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "utils/fileutils.h"
#include "utils/strutils.h"

/*
 * amdgpu-specific sysfs attributes (kernel Documentation/gpu/amdgpu/
 * driver docs): mem_info_vram_total/used (bytes) and gpu_busy_percent
 * (0-100 integer) live directly under the device directory. There is no
 * standard sysfs attribute for a human-readable marketing name (unlike
 * NVML's nvmlDeviceGetName) — rather than guess or hardcode a PCI-ID to
 * marketing-name table this project cannot keep accurate, the name is
 * honestly reported as the driver + PCI device ID.
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

/* Finds this device's own hwmon child (device/hwmon/hwmonN/) and reads
 * its first temperature input. amdgpu registers exactly one hwmon device
 * per card exposing edge/junction/memory temps as temp1/2/3_input; we
 * report temp1 (the primary "edge" sensor on essentially every card) —
 * finer per-sensor detail belongs to the general sensors module
 * (Phase 7), which already surfaces all of them generically. */
static int read_temperature(const char *card_path, double *out_celsius)
{
    char hwmon_dir_path[320];
    snprintf(hwmon_dir_path, sizeof(hwmon_dir_path), "%s/device/hwmon", card_path);

    DIR *dir = opendir(hwmon_dir_path);
    if (dir == NULL)
        return 0;

    struct dirent *entry;
    int found = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        /* Sized for the worst case GCC's -Wformat-truncation can prove:
         * entry->d_name is only bounded at NAME_MAX by the dirent(3)
         * interface, even though real hwmon directory names ("hwmon0")
         * are always short. See disk.c's LSM_SYSFS_PATH_SIZE for the
         * same reasoning. */
        char temp_path[sizeof(hwmon_dir_path) + NAME_MAX + sizeof("/temp1_input")];
        snprintf(temp_path, sizeof(temp_path), "%s/%s/temp1_input",
                 hwmon_dir_path, entry->d_name);

        long millidegrees = 0;
        if (lsm_read_file_long(temp_path, &millidegrees) == LSM_OK) {
            *out_celsius = (double)millidegrees / 1000.0;
            found = 1;
            break;
        }
    }

    closedir(dir);
    return found;
}

lsm_status_t lsm_gpu_amd_collect(lsm_gpu_list_t *list, const char *card_path,
                                    const char *driver)
{
    lsm_gpu_info_t info;
    memset(&info, 0, sizeof(info));
    info.vendor = LSM_GPU_VENDOR_AMD;
    lsm_strlcpy(info.driver, driver, sizeof(info.driver));

    char device_id[16];
    read_pci_device_id(card_path, device_id, sizeof(device_id));
    snprintf(info.name, sizeof(info.name), "AMD GPU (PCI ID 1002:%s)", device_id);

    char path[320];

    long vram_total = 0, vram_used = 0;
    snprintf(path, sizeof(path), "%s/device/mem_info_vram_total", card_path);
    lsm_status_t total_status = lsm_read_file_long(path, &vram_total);
    snprintf(path, sizeof(path), "%s/device/mem_info_vram_used", card_path);
    lsm_status_t used_status = lsm_read_file_long(path, &vram_used);
    if (total_status == LSM_OK && used_status == LSM_OK) {
        info.has_memory = 1;
        info.memory_total_kib = (uint64_t)vram_total / 1024;
        info.memory_used_kib = (uint64_t)vram_used / 1024;
    }

    snprintf(path, sizeof(path), "%s/device/gpu_busy_percent", card_path);
    long busy_percent = 0;
    if (lsm_read_file_long(path, &busy_percent) == LSM_OK) {
        info.has_utilization_percent = 1;
        info.utilization_percent = (double)busy_percent;
    }

    double temp_celsius = 0.0;
    if (read_temperature(card_path, &temp_celsius)) {
        info.has_temperature = 1;
        info.temperature_c = temp_celsius;
    }

    return lsm_gpu_list_append(list, &info);
}
