#ifndef LSM_UTILS_FORMAT_H
#define LSM_UTILS_FORMAT_H

#include <stddef.h>
#include <stdint.h>

/*
 * Human-readable formatting helpers shared by the CLI and, later, the TUI.
 * Kept binary (KiB/MiB/GiB, powers of 1024) since every kernel data source
 * we read (/proc/meminfo, statvfs, etc.) already reports in KiB.
 */

/* Formats `kib` KiB as e.g. "15.6 GiB" into `out` (outsize >= 32 recommended). */
void lsm_format_kib(uint64_t kib, char *out, size_t outsize);

/* Formats `seconds` as e.g. "3d 04:12:07" into `out` (outsize >= 32 recommended). */
void lsm_format_duration(double seconds, char *out, size_t outsize);

#endif /* LSM_UTILS_FORMAT_H */
