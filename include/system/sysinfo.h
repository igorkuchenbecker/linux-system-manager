#ifndef LSM_SYSTEM_SYSINFO_H
#define LSM_SYSTEM_SYSINFO_H

#include <stdint.h>

#include "core/status.h"

/*
 * Phase 1 — System Information.
 *
 * Sources (documented per-field in sysinfo.c):
 *   uname(2), sysconf(3), /etc/os-release, /proc/cpuinfo,
 *   /proc/uptime, /proc/meminfo.
 *
 * Design: every string/numeric field that could not be determined is left
 * at a well-defined fallback ("N/A" / 0) rather than aborting the whole
 * collection — see core/status.h and the project-wide rule that a missing
 * data source must degrade gracefully, not crash the program. The overall
 * lsm_status_t return communicates whether *any* partial failure occurred,
 * so callers that care can log it; callers that don't can ignore it and
 * still get a fully-populated, safely-defaulted struct.
 */
typedef struct lsm_system_info {
    char hostname[256];        /* uname().nodename */
    char kernel_name[65];      /* uname().sysname, e.g. "Linux" */
    char kernel_release[65];   /* uname().release, e.g. "6.10.6-arch1-1" */
    char kernel_version[256];  /* uname().version, build date/flags */
    char architecture[65];     /* uname().machine, e.g. "x86_64" */

    char os_id[64];            /* /etc/os-release ID=, e.g. "arch" */
    char os_pretty_name[192];  /* /etc/os-release PRETTY_NAME= */

    char cpu_model[256];       /* /proc/cpuinfo "model name" (x86) or fallback */
    long cpu_logical_count;    /* sysconf(_SC_NPROCESSORS_ONLN): online cores/threads */

    uint64_t ram_total_kib;    /* /proc/meminfo MemTotal, in KiB as reported by kernel */

    double uptime_seconds;     /* /proc/uptime, first field */
} lsm_system_info_t;

/*
 * Populates `info` with a best-effort snapshot of static/near-static system
 * information. Never leaves fields uninitialized: on partial failure the
 * affected field is set to a safe fallback ("N/A", 0, etc.) and collection
 * continues with the remaining fields.
 *
 * Returns LSM_OK if every field was obtained; otherwise returns the status
 * of the *first* failure encountered, purely as a diagnostic hint — the
 * struct is still fully usable.
 */
lsm_status_t lsm_sysinfo_collect(lsm_system_info_t *info);

#endif /* LSM_SYSTEM_SYSINFO_H */
