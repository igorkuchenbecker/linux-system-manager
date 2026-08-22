#include "cpu/cpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/log.h"
#include "utils/fileutils.h"
#include "utils/strutils.h"

#define LSM_TAG "cpu"

/*
 * /proc/stat can grow to tens of KB on high core-count machines (each
 * "cpuN ..." line is roughly 60-70 bytes). 64 KiB comfortably covers
 * several hundred cores' worth of lines plus the trailing non-cpu stats
 * (intr, ctxt, btime, ...) that follow them in the same file.
 */
#define STAT_BUF_SIZE (64 * 1024)

/*
 * Parses one "cpu[ N] u n s i iow irq sirq steal guest guest_nice" line.
 * Only the leading 4 fields (user/nice/system/idle) are guaranteed to
 * exist on every kernel version; the rest were added incrementally
 * (iowait in 2.5.41, irq/softirq later, steal in 2.6.11, guest in 2.6.24,
 * guest_nice in 2.6.33). Fields absent on an old kernel are left at 0,
 * which is the correct value for "this counter did not exist yet".
 */
static int parse_cpu_line(const char *value_start, lsm_cpu_times_t *out)
{
    memset(out, 0, sizeof(*out));

    int n = sscanf(value_start, "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                    &out->user, &out->nice, &out->system, &out->idle,
                    &out->iowait, &out->irq, &out->softirq, &out->steal,
                    &out->guest, &out->guest_nice);

    /* user/nice/system/idle are the historical minimum every kernel has. */
    return n >= 4;
}

lsm_status_t lsm_cpu_read_snapshot(lsm_cpu_snapshot_t *snapshot)
{
    if (snapshot == NULL)
        return LSM_ERR_INVALID_ARG;

    memset(snapshot, 0, sizeof(*snapshot));

    char *buf = malloc(STAT_BUF_SIZE);
    if (buf == NULL)
        return LSM_ERR_NOMEM;

    size_t len = 0;
    lsm_status_t status = lsm_read_file("/proc/stat", buf, STAT_BUF_SIZE, &len);
    if (status != LSM_OK) {
        free(buf);
        return status;
    }

    int have_aggregate = 0;
    char *saveptr = NULL;
    char *line = strtok_r(buf, "\n", &saveptr);
    while (line != NULL) {
        if (lsm_str_has_prefix(line, "cpu")) {
            const char *rest = line + 3;

            if (*rest == ' ' || *rest == '\t') {
                /* Aggregate "cpu " line. */
                if (parse_cpu_line(rest, &snapshot->aggregate))
                    have_aggregate = 1;
                else
                    LSM_LOGW(LSM_TAG, "failed to parse aggregate line in /proc/stat");
            } else if (*rest >= '0' && *rest <= '9') {
                /* Per-core "cpuN " line. */
                char *end = NULL;
                long idx = strtol(rest, &end, 10);
                if (idx >= 0 && (size_t)idx < LSM_CPU_MAX_CORES && end != rest) {
                    lsm_cpu_times_t times;
                    if (parse_cpu_line(end, &times)) {
                        snapshot->per_core[idx] = times;
                        if ((size_t)idx + 1 > snapshot->core_count)
                            snapshot->core_count = (size_t)idx + 1;
                    } else {
                        LSM_LOGW(LSM_TAG, "failed to parse cpu%ld line in /proc/stat", idx);
                    }
                } else if (idx >= (long)LSM_CPU_MAX_CORES) {
                    LSM_LOGW(LSM_TAG, "cpu%ld exceeds LSM_CPU_MAX_CORES (%d), truncating",
                              idx, LSM_CPU_MAX_CORES);
                }
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(buf);

    if (!have_aggregate)
        return LSM_ERR_PARSE;
    return LSM_OK;
}

double lsm_cpu_times_usage_percent(const lsm_cpu_times_t *prev,
                                    const lsm_cpu_times_t *curr)
{
    unsigned long long prev_idle = prev->idle + prev->iowait;
    unsigned long long curr_idle = curr->idle + curr->iowait;

    unsigned long long prev_total = lsm_cpu_times_total_ticks(prev);
    unsigned long long curr_total = lsm_cpu_times_total_ticks(curr);

    /* Guards against a shorter-than-one-tick sampling interval, and
     * against curr < prev (e.g. counters read across a suspend/resume
     * where the kernel's accounting base shifted) — both must yield 0.0,
     * not a nonsensical negative or division-by-zero result. */
    if (curr_total <= prev_total || curr_idle < prev_idle)
        return 0.0;

    unsigned long long total_delta = curr_total - prev_total;
    unsigned long long idle_delta = curr_idle - prev_idle;

    if (idle_delta > total_delta)
        return 0.0;

    return (double)(total_delta - idle_delta) * 100.0 / (double)total_delta;
}

unsigned long long lsm_cpu_times_total_ticks(const lsm_cpu_times_t *times)
{
    return times->user + times->nice + times->system + times->idle +
           times->iowait + times->irq + times->softirq + times->steal;
}

lsm_status_t lsm_cpu_load_average(double *load1, double *load5, double *load15)
{
    if (load1 == NULL || load5 == NULL || load15 == NULL)
        return LSM_ERR_INVALID_ARG;

    *load1 = *load5 = *load15 = 0.0;

    char buf[128];
    lsm_status_t status = lsm_read_file("/proc/loadavg", buf, sizeof(buf), NULL);
    if (status != LSM_OK)
        return status;

    if (sscanf(buf, "%lf %lf %lf", load1, load5, load15) != 3)
        return LSM_ERR_PARSE;

    return LSM_OK;
}

lsm_status_t lsm_cpu_core_frequency_mhz(size_t core_index, double *out_mhz)
{
    if (out_mhz == NULL)
        return LSM_ERR_INVALID_ARG;

    *out_mhz = 0.0;

    char path[96];
    snprintf(path, sizeof(path),
             "/sys/devices/system/cpu/cpu%zu/cpufreq/scaling_cur_freq", core_index);

    long khz = 0;
    lsm_status_t status = lsm_read_file_long(path, &khz);
    if (status != LSM_OK) {
        /* No cpufreq sysfs entry at all -> genuinely unsupported here,
         * not a transient I/O error. */
        if (status == LSM_ERR_NOT_FOUND)
            return LSM_ERR_UNSUPPORTED;
        return status;
    }

    *out_mhz = (double)khz / 1000.0;
    return LSM_OK;
}
