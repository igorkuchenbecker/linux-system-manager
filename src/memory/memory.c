#include "memory/memory.h"

#include <stdio.h>
#include <string.h>

#include "utils/fileutils.h"
#include "utils/strutils.h"

/*
 * /proc/meminfo has ~50-60 lines on a typical kernel, each well under 40
 * bytes ("SomeKey:  123456789 kB\n"). 8 KiB is generous headroom.
 */
#define MEMINFO_BUF_SIZE 8192

/*
 * Matches a line of the form "<key>:<spaces><digits> kB" (the "kB"
 * suffix is present on every size field /proc/meminfo reports; a few
 * fields like HugePages_Total have no suffix, but we never read those
 * here). Returns 1 and writes to `out` on match, 0 otherwise.
 */
static int match_meminfo_key(const char *line, const char *key, uint64_t *out)
{
    size_t key_len = strlen(key);
    if (strncmp(line, key, key_len) != 0 || line[key_len] != ':')
        return 0;

    unsigned long long value = 0;
    if (sscanf(line + key_len + 1, "%llu", &value) != 1)
        return 0;

    *out = (uint64_t)value;
    return 1;
}

lsm_status_t lsm_memory_collect(lsm_memory_info_t *info)
{
    if (info == NULL)
        return LSM_ERR_INVALID_ARG;

    memset(info, 0, sizeof(*info));

    char buf[MEMINFO_BUF_SIZE];
    lsm_status_t status = lsm_read_file("/proc/meminfo", buf, sizeof(buf), NULL);
    if (status != LSM_OK)
        return status;

    int have_total = 0, have_free = 0, have_available = 0;
    int have_buffers = 0, have_cached = 0;
    int have_swap_total = 0, have_swap_free = 0;

    char *saveptr = NULL;
    char *line = strtok_r(buf, "\n", &saveptr);
    while (line != NULL) {
        uint64_t value = 0;

        if (match_meminfo_key(line, "MemTotal", &value)) {
            info->total_kib = value;
            have_total = 1;
        } else if (match_meminfo_key(line, "MemFree", &value)) {
            info->free_kib = value;
            have_free = 1;
        } else if (match_meminfo_key(line, "MemAvailable", &value)) {
            info->available_kib = value;
            have_available = 1;
        } else if (match_meminfo_key(line, "Buffers", &value)) {
            info->buffers_kib = value;
            have_buffers = 1;
        } else if (match_meminfo_key(line, "Cached", &value)) {
            info->cached_kib = value;
            have_cached = 1;
        } else if (match_meminfo_key(line, "SwapTotal", &value)) {
            info->swap_total_kib = value;
            have_swap_total = 1;
        } else if (match_meminfo_key(line, "SwapFree", &value)) {
            info->swap_free_kib = value;
            have_swap_free = 1;
        }

        line = strtok_r(NULL, "\n", &saveptr);
    }

    if (!have_total || !have_free)
        return LSM_ERR_PARSE;

    if (!have_available) {
        /* Pre-3.14 kernel fallback: the classic (less accurate)
         * approximation used before the kernel computed this itself. */
        info->available_kib = info->free_kib +
            (have_buffers ? info->buffers_kib : 0) +
            (have_cached ? info->cached_kib : 0);
        info->available_is_estimated = 1;
    }

    info->used_kib = (info->total_kib > info->available_kib)
        ? info->total_kib - info->available_kib
        : 0;

    if (have_swap_total && have_swap_free) {
        info->swap_used_kib = (info->swap_total_kib > info->swap_free_kib)
            ? info->swap_total_kib - info->swap_free_kib
            : 0;
    }

    if (!have_buffers || !have_cached || !have_swap_total || !have_swap_free)
        return LSM_ERR_PARSE;

    return LSM_OK;
}
