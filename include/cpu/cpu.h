#ifndef LSM_CPU_CPU_H
#define LSM_CPU_CPU_H

#include <stddef.h>

#include "core/status.h"

/*
 * Phase 2 — CPU Monitor.
 *
 * Sources: /proc/stat (per-CPU jiffie counters, used for usage%),
 * /proc/loadavg (load average), /sys/devices/system/cpu/cpuN/cpufreq
 * (current frequency, when a cpufreq governor is active).
 *
 * CPU usage is fundamentally a *rate*, not an instantaneous value: the
 * kernel only exposes cumulative jiffie counters since boot. Correct usage
 * percentages require two snapshots and a diff — see
 * lsm_cpu_times_usage_percent(). There is no "current CPU usage" syscall;
 * every tool (top, htop, this one) computes it the same way.
 */

/*
 * Cumulative jiffie counters for one CPU (aggregate "cpu" line or a single
 * "cpuN" line) since boot, as reported by /proc/stat. Units are USER_HZ
 * jiffies (sysconf(_SC_CLK_TCK) per second), not seconds or nanoseconds —
 * only ratios between two snapshots are meaningful, not absolute values.
 *
 * Field set matches Documentation/filesystems/proc.rst. guest/guest_nice
 * are already included in user/nice on kernels that report them (per the
 * kernel doc), we store them separately only because /proc/stat does.
 */
typedef struct lsm_cpu_times {
    unsigned long long user;
    unsigned long long nice;
    unsigned long long system;
    unsigned long long idle;
    unsigned long long iowait;
    unsigned long long irq;
    unsigned long long softirq;
    unsigned long long steal;
    unsigned long long guest;
    unsigned long long guest_nice;
} lsm_cpu_times_t;

/*
 * Upper bound on logical CPUs this snapshot can hold. 256 comfortably
 * covers current desktop/workstation/most-server hardware; very large
 * multi-socket servers with more logical CPUs will have their extra cores
 * silently truncated (lsm_cpu_snapshot_t.core_count reports how many were
 * actually captured — never more than this). Raising this constant is
 * safe and free (only enlarges an array inside a heap/caller-owned
 * struct, never a raw stack buffer) if that becomes a real constraint.
 */
#define LSM_CPU_MAX_CORES 256

typedef struct lsm_cpu_snapshot {
    lsm_cpu_times_t aggregate;                  /* "cpu" line: sum across all cores */
    lsm_cpu_times_t per_core[LSM_CPU_MAX_CORES]; /* "cpu0".."cpuN" lines, in order */
    size_t core_count;                           /* valid entries in per_core */
} lsm_cpu_snapshot_t;

/*
 * Reads /proc/stat and fills `snapshot`. Always fully initializes
 * `snapshot` (zeroed) even on failure. A malformed individual "cpuN" line
 * is skipped (logged, not fatal) rather than aborting the whole read.
 */
lsm_status_t lsm_cpu_read_snapshot(lsm_cpu_snapshot_t *snapshot);

/*
 * Computes CPU usage percentage (0.0-100.0) between two snapshots of the
 * *same* CPU (both aggregate, or both the same core index) taken at
 * different times. `prev` must have been sampled strictly before `curr`.
 * Returns 0.0 if the two snapshots are identical (no elapsed jiffies) —
 * this happens when the sampling interval is shorter than one tick.
 */
double lsm_cpu_times_usage_percent(const lsm_cpu_times_t *prev,
                                    const lsm_cpu_times_t *curr);

/*
 * Sum of all counted ticks in one snapshot (the same denominator used
 * internally by lsm_cpu_times_usage_percent()). Exposed so higher layers
 * (e.g. the process manager, composing per-process CPU% against the
 * system-wide tick delta) can normalize without duplicating this formula
 * or reaching into lsm_cpu_times_t's field set themselves.
 */
unsigned long long lsm_cpu_times_total_ticks(const lsm_cpu_times_t *times);

/* /proc/loadavg: 1/5/15-minute load averages (runnable+uninterruptible
 * task count, exponentially damped, same semantics as uptime(1)). */
lsm_status_t lsm_cpu_load_average(double *load1, double *load5, double *load15);

/*
 * Current frequency of logical CPU `core_index`, in MHz, from
 * /sys/devices/system/cpu/cpuN/cpufreq/scaling_cur_freq (kHz in the
 * kernel's units, converted here). Returns LSM_ERR_UNSUPPORTED if no
 * cpufreq governor is active for this core (common in some VMs/containers
 * and on some ARM boards) — callers must treat that as "N/A", not fatal.
 */
lsm_status_t lsm_cpu_core_frequency_mhz(size_t core_index, double *out_mhz);

#endif /* LSM_CPU_CPU_H */
