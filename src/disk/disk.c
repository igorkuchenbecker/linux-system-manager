#include "disk/disk.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>

#include "core/log.h"
#include "utils/fileutils.h"
#include "utils/strutils.h"

#define LSM_TAG "disk"

/* /proc/mounts is small (one line per mount, rarely more than a few
 * hundred), but containers/LVM-heavy systems can have hundreds of
 * bind-mounts — 32 KiB is comfortable headroom. */
#define MOUNTS_BUF_SIZE (32 * 1024)

/*
 * The kernel mangles whitespace and backslashes in /proc/mounts fields
 * (mangle_path() in fs/proc_namespace.c) as octal escapes: space -> \040,
 * tab -> \011, backslash -> \134, newline -> \012. Unescapes in place;
 * the result is always <= the input length so this never overflows.
 */
static void unescape_octal(char *s)
{
    char *read_ptr = s;
    char *write_ptr = s;

    while (*read_ptr != '\0') {
        if (read_ptr[0] == '\\' && read_ptr[1] >= '0' && read_ptr[1] <= '7' &&
            read_ptr[2] >= '0' && read_ptr[2] <= '7' && read_ptr[3] >= '0' && read_ptr[3] <= '7') {
            int value = (read_ptr[1] - '0') * 64 + (read_ptr[2] - '0') * 8 + (read_ptr[3] - '0');
            *write_ptr++ = (char)value;
            read_ptr += 4;
        } else {
            *write_ptr++ = *read_ptr++;
        }
    }
    *write_ptr = '\0';
}

/*
 * Pseudo-filesystems with no meaningful storage capacity to report.
 * Not exhaustive by design: an unrecognized fstype is included rather
 * than hidden, per the header's documented policy.
 */
static int is_pseudo_filesystem(const char *fstype)
{
    static const char *pseudo[] = {
        "proc", "sysfs", "cgroup", "cgroup2", "devpts", "devtmpfs",
        "pstore", "securityfs", "debugfs", "tracefs", "configfs",
        "fusectl", "mqueue", "hugetlbfs", "bpf", "autofs", "binfmt_misc",
        "rpc_pipefs", "nsfs", "sunrpc", "efivarfs", "selinuxfs", "ramfs",
        NULL,
    };
    for (int i = 0; pseudo[i] != NULL; i++) {
        if (strcmp(fstype, pseudo[i]) == 0)
            return 1;
    }
    return 0;
}

static lsm_status_t ensure_mount_capacity(lsm_mount_list_t *list)
{
    if (list->count < list->capacity)
        return LSM_OK;

    size_t new_capacity = (list->capacity == 0) ? 32 : list->capacity * 2;
    lsm_mount_info_t *grown = realloc(list->items, new_capacity * sizeof(*grown));
    if (grown == NULL)
        return LSM_ERR_NOMEM;

    list->items = grown;
    list->capacity = new_capacity;
    return LSM_OK;
}

lsm_status_t lsm_disk_mounts_collect(lsm_mount_list_t *list)
{
    if (list == NULL)
        return LSM_ERR_INVALID_ARG;

    lsm_disk_mounts_free(list);

    char *buf = malloc(MOUNTS_BUF_SIZE);
    if (buf == NULL)
        return LSM_ERR_NOMEM;

    lsm_status_t status = lsm_read_file("/proc/mounts", buf, MOUNTS_BUF_SIZE, NULL);
    if (status != LSM_OK) {
        free(buf);
        return status;
    }

    char *saveptr = NULL;
    char *line = strtok_r(buf, "\n", &saveptr);
    while (line != NULL) {
        char device[160], mount_point[256], fstype[32];
        if (sscanf(line, "%159s %255s %31s", device, mount_point, fstype) != 3) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }
        unescape_octal(device);
        unescape_octal(mount_point);

        if (is_pseudo_filesystem(fstype)) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        struct statvfs vfs;
        if (statvfs(mount_point, &vfs) != 0) {
            /*
             * Unmounted mid-scan, unreachable network mount, or
             * permission denied on the mount point — skip, don't abort.
             * EACCES/EPERM are routine, not exceptional: e.g. every
             * systemd-managed machine has root-only credential mounts
             * under /run/credentials/ (one per service) that an unprivileged
             * caller (or a sandboxed daemon with ProtectSystem=strict,
             * observed to behave differently here from an unsandboxed
             * process during Phase 10 testing) cannot statvfs(). Match
             * process.c's philosophy: expected per-item skips are not
             * logged as errors, only genuinely unexpected I/O failures are.
             */
            if (errno != EACCES && errno != EPERM && errno != ENOENT)
                lsm_log_errno(LSM_TAG, mount_point, errno);
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        if (ensure_mount_capacity(list) != LSM_OK) {
            free(buf);
            lsm_disk_mounts_free(list);
            return LSM_ERR_NOMEM;
        }

        lsm_mount_info_t *entry = &list->items[list->count];
        lsm_strlcpy(entry->device, device, sizeof(entry->device));
        lsm_strlcpy(entry->mount_point, mount_point, sizeof(entry->mount_point));
        lsm_strlcpy(entry->filesystem, fstype, sizeof(entry->filesystem));

        uint64_t block_kib = (uint64_t)vfs.f_frsize / 1024;
        if (block_kib == 0)
            block_kib = 1; /* f_frsize < 1024 on some virtual filesystems */
        uint64_t total_kib = (uint64_t)vfs.f_blocks * block_kib;
        uint64_t free_kib = (uint64_t)vfs.f_bfree * block_kib;
        uint64_t available_kib = (uint64_t)vfs.f_bavail * block_kib;

        entry->total_kib = total_kib;
        entry->available_kib = available_kib;
        entry->used_kib = (total_kib > free_kib) ? total_kib - free_kib : 0;

        list->count++;
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(buf);
    return LSM_OK;
}

void lsm_disk_mounts_free(lsm_mount_list_t *list)
{
    if (list == NULL)
        return;
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static lsm_status_t ensure_device_capacity(lsm_disk_device_list_t *list)
{
    if (list->count < list->capacity)
        return LSM_OK;

    size_t new_capacity = (list->capacity == 0) ? 16 : list->capacity * 2;
    lsm_disk_device_t *grown = realloc(list->items, new_capacity * sizeof(*grown));
    if (grown == NULL)
        return LSM_ERR_NOMEM;

    list->items = grown;
    list->capacity = new_capacity;
    return LSM_OK;
}

/*
 * /sys/block/<dev>/stat: 11+ whitespace-separated fields (iostats.rst).
 * We only need read_ios(1st), read_sectors(3rd), write_ios(5th),
 * write_sectors(7th); the rest (merges, ticks, in-flight, queue time,
 * and the newer discard/flush fields) are not needed for Phase 5's
 * throughput reporting.
 */
/*
 * Path buffers here are sized for the worst case GCC's -Wformat-truncation
 * can prove: `name` comes from a struct dirent's d_name, which the
 * dirent(3) interface only bounds at NAME_MAX (255) — even though real
 * block device names are always short, the compiler cannot know that
 * from the (unbounded, as far as it can prove) `const char *` parameter.
 */
#define LSM_SYSFS_PATH_SIZE (sizeof("/sys/block/") + NAME_MAX + sizeof("/device/model"))

static lsm_status_t read_device_stat(const char *name, lsm_disk_device_t *out)
{
    char path[LSM_SYSFS_PATH_SIZE];
    snprintf(path, sizeof(path), "/sys/block/%s/stat", name);

    char buf[512];
    lsm_status_t status = lsm_read_file(path, buf, sizeof(buf), NULL);
    if (status != LSM_OK)
        return status;

    unsigned long long read_ios, read_merges, read_sectors, read_ticks;
    unsigned long long write_ios, write_merges, write_sectors;

    int n = sscanf(buf, "%llu %llu %llu %llu %llu %llu %llu",
                    &read_ios, &read_merges, &read_sectors, &read_ticks,
                    &write_ios, &write_merges, &write_sectors);
    if (n != 7)
        return LSM_ERR_PARSE;

    out->read_ios = read_ios;
    out->read_sectors = read_sectors;
    out->write_ios = write_ios;
    out->write_sectors = write_sectors;
    return LSM_OK;
}

static void read_device_model(const char *name, char *out, size_t out_size)
{
    char path[LSM_SYSFS_PATH_SIZE];
    snprintf(path, sizeof(path), "/sys/block/%s/device/model", name);

    char buf[160];
    lsm_status_t status = lsm_read_file(path, buf, sizeof(buf), NULL);
    if (status != LSM_OK) {
        /* Virtual devices (loop, dm-*, zram, md, ramdisk) have no
         * "device/" subtree at all — this is normal, not an error. */
        lsm_strlcpy(out, "N/A", out_size);
        return;
    }
    lsm_strlcpy(out, lsm_str_trim(buf), out_size);
}

static int read_device_removable(const char *name)
{
    char path[LSM_SYSFS_PATH_SIZE];
    snprintf(path, sizeof(path), "/sys/block/%s/removable", name);

    long value = 0;
    if (lsm_read_file_long(path, &value) != LSM_OK)
        return 0;
    return value != 0;
}

lsm_status_t lsm_disk_devices_collect(lsm_disk_device_list_t *list)
{
    if (list == NULL)
        return LSM_ERR_INVALID_ARG;

    lsm_disk_devices_free(list);

    DIR *dir = opendir("/sys/block");
    if (dir == NULL) {
        /* /sys/block not mounted at all is genuinely unsupported (e.g. a
         * container without sysfs), not a hard error. */
        if (errno == ENOENT)
            return LSM_ERR_UNSUPPORTED;
        lsm_log_errno(LSM_TAG, "/sys/block", errno);
        return LSM_ERR_IO;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        lsm_disk_device_t device;
        memset(&device, 0, sizeof(device));
        lsm_strlcpy(device.name, entry->d_name, sizeof(device.name));

        lsm_status_t status = read_device_stat(entry->d_name, &device);
        if (status != LSM_OK) {
            LSM_LOGW(LSM_TAG, "skipping %s: %s", entry->d_name, lsm_status_str(status));
            continue;
        }

        read_device_model(entry->d_name, device.model, sizeof(device.model));
        device.removable = read_device_removable(entry->d_name);

        if (ensure_device_capacity(list) != LSM_OK) {
            closedir(dir);
            lsm_disk_devices_free(list);
            return LSM_ERR_NOMEM;
        }
        list->items[list->count++] = device;
    }

    closedir(dir);
    return LSM_OK;
}

void lsm_disk_devices_free(lsm_disk_device_list_t *list)
{
    if (list == NULL)
        return;
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}
