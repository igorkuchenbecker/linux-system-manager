#ifndef LSM_DISK_DISK_H
#define LSM_DISK_DISK_H

#include <stddef.h>
#include <stdint.h>

#include "core/status.h"

/*
 * Phase 5 — Disk Monitor.
 *
 * Two independent sub-areas, deliberately kept as separate collectors
 * (mirrors how the kernel itself separates them — filesystems and block
 * devices are different subsystems with no fixed 1:1 mapping: one block
 * device can host many filesystems via partitions/LVM/LUKS, and one
 * filesystem (tmpfs, overlay, nfs) may have no backing block device at
 * all):
 *
 *   - lsm_disk_mounts_collect()  — per-mount-point capacity/usage, via
 *     /proc/mounts (device, mount point, fstype) + statvfs(3) (space).
 *   - lsm_disk_devices_collect() — per-block-device cumulative I/O
 *     counters, via /sys/block/<dev>/stat. Rate (bytes/sec) is, like CPU
 *     usage, a diff of two samples — this module exposes only the raw
 *     cumulative sector counts; the caller diffs and divides by elapsed
 *     wall-clock time (see docs/ARCHITECTURE.md's composition note).
 */

/*
 * free vs. available (statvfs' f_bfree vs f_bavail): most filesystems
 * reserve a slice of space (ext4's default 5% root reserve, for example)
 * that only privileged processes may use. `available_kib` mirrors what
 * df(1) reports in its "Avail" column and is what an ordinary user
 * process can actually still write; `total_kib - used_kib` and
 * `used_kib + available_kib` therefore need not be equal — the gap is
 * exactly that reserved slice. This is analogous to the free/available
 * RAM distinction documented in memory/memory.h.
 */
typedef struct lsm_mount_info {
    char device[160];        /* e.g. "/dev/nvme0n1p2", "tmpfs", "overlay" */
    char mount_point[256];
    char filesystem[32];      /* e.g. "ext4", "btrfs", "vfat", "tmpfs" */
    uint64_t total_kib;
    uint64_t used_kib;         /* total - free (may include the reserved slice) */
    uint64_t available_kib;    /* statvfs f_bavail: actually usable by this process */
} lsm_mount_info_t;

typedef struct lsm_mount_list {
    lsm_mount_info_t *items;  /* heap-allocated, owned by this struct */
    size_t count;
    size_t capacity;
} lsm_mount_list_t;

/*
 * Enumerates mounted filesystems from /proc/mounts and augments each with
 * space usage via statvfs(3). `list` must be zero-initialized or
 * previously freed. Filesystem types with no meaningful storage capacity
 * (proc, sysfs, cgroup, devpts, ...) are skipped via a known-pseudo-fs
 * denylist (see disk.c); unrecognized types are included by default —
 * better to show an unfamiliar real filesystem than to silently hide it.
 * A mount point that fails statvfs() (e.g. unmounted between the two
 * reads, or an unreachable network mount) is skipped rather than
 * aborting the whole enumeration.
 */
lsm_status_t lsm_disk_mounts_collect(lsm_mount_list_t *list);

/* Releases the array owned by `list` and zeroes it. Safe to call twice. */
void lsm_disk_mounts_free(lsm_mount_list_t *list);

/*
 * Cumulative I/O counters for one block device, straight from
 * /sys/block/<dev>/stat (Documentation/admin-guide/iostats.rst). Sector
 * counts are always in 512-byte units regardless of the device's actual
 * logical block size — a kernel-wide convention for this file, not a
 * property of the device — so byte counts are `sectors * 512`.
 */
typedef struct lsm_disk_device {
    char name[32];             /* e.g. "sda", "nvme0n1"; partitions (sda1) excluded */
    char model[160];            /* /sys/block/<dev>/device/model, trimmed; "N/A" if absent
                                  * (common for virtual devices: loop, dm-*, zram, md) */
    int removable;               /* /sys/block/<dev>/removable: 1 if hot-pluggable (USB, SD) */
    unsigned long long read_ios;
    unsigned long long read_sectors;
    unsigned long long write_ios;
    unsigned long long write_sectors;
} lsm_disk_device_t;

typedef struct lsm_disk_device_list {
    lsm_disk_device_t *items;
    size_t count;
    size_t capacity;
} lsm_disk_device_list_t;

/*
 * Enumerates block devices from /sys/block/. Only whole devices are
 * listed (partitions like "sda1" are children of "sda" in sysfs and are
 * intentionally excluded here — they share the same physical throughput
 * as their parent device; per-partition space usage is already covered
 * by lsm_disk_mounts_collect() for whichever partitions are mounted).
 */
lsm_status_t lsm_disk_devices_collect(lsm_disk_device_list_t *list);

/* Releases the array owned by `list` and zeroes it. Safe to call twice. */
void lsm_disk_devices_free(lsm_disk_device_list_t *list);

#endif /* LSM_DISK_DISK_H */
