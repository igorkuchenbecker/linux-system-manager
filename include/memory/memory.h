#ifndef LSM_MEMORY_MEMORY_H
#define LSM_MEMORY_MEMORY_H

#include <stdint.h>

#include "core/status.h"

/*
 * Phase 3 — Memory Monitor.
 *
 * Source: /proc/meminfo exclusively. All values are in KiB, matching the
 * unit /proc/meminfo actually reports (labeled "kB" for historical
 * reasons, but kernel source treats it as 1024-byte units).
 *
 * IMPORTANT — free vs. available vs. used, and why "used RAM" is not bad:
 *
 *   - `free_kib`      MemFree: pages truly untouched by any allocation.
 *                      On a healthy long-running Linux system this number
 *                      is often small and *that is fine* — Linux uses
 *                      spare RAM as page cache rather than leaving it idle.
 *   - `buffers_kib`   Buffers: raw block-device I/O buffers (metadata),
 *                      typically small.
 *   - `cached_kib`    Cached: page cache holding file contents (source
 *                      code, libraries, previously-read files). This
 *                      memory is reclaimed instantly under pressure —
 *                      it is not "used" in any sense that hurts you.
 *   - `available_kib` MemAvailable (kernel-computed since Linux 3.14):
 *                      an estimate of memory available to start a new
 *                      application *without* swapping, accounting for
 *                      the fact that some cache/buffers are reclaimable
 *                      and some are not (e.g. dirty pages, shared memory
 *                      pinned by running processes). This is the figure
 *                      user-facing tools should treat as "how much RAM
 *                      do I actually have left" — NOT `free_kib`.
 *   - `used_kib`      Derived here as `total_kib - available_kib`: the
 *                      complement of the kernel's own "available"
 *                      estimate. This project deliberately does NOT use
 *                      the naive `total - free` formula, because that
 *                      formula counts reclaimable cache as "used" and
 *                      produces the classic, misleading "why is 90% of
 *                      my RAM used?!" reading.
 *
 * On pre-3.14 kernels lacking MemAvailable, we fall back to the classic
 * approximation `free + buffers + cached` (what the original `free(1)`
 * used before the kernel started computing it directly) and the fallback
 * is recorded by returning LSM_ERR_UNSUPPORTED for that specific field's
 * derivation quality — the struct is still fully populated.
 */
typedef struct lsm_memory_info {
    uint64_t total_kib;
    uint64_t free_kib;
    uint64_t available_kib;
    uint64_t buffers_kib;
    uint64_t cached_kib;
    uint64_t used_kib;        /* derived: total_kib - available_kib */

    uint64_t swap_total_kib;
    uint64_t swap_free_kib;
    uint64_t swap_used_kib;   /* derived: swap_total_kib - swap_free_kib */

    int available_is_estimated; /* 1 if MemAvailable was missing and we
                                  * fell back to free+buffers+cached */
} lsm_memory_info_t;

/*
 * Populates `info` from /proc/meminfo. Always fully initializes `info`
 * (zeroed) even on failure. Returns LSM_OK on success; LSM_ERR_* if
 * /proc/meminfo itself could not be read or a required key was absent.
 */
lsm_status_t lsm_memory_collect(lsm_memory_info_t *info);

#endif /* LSM_MEMORY_MEMORY_H */
