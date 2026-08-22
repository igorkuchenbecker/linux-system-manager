#include "system/sysinfo.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "core/log.h"
#include "utils/fileutils.h"
#include "utils/strutils.h"

#define LSM_TAG "sysinfo"

static void set_first_failure(lsm_status_t *first, lsm_status_t candidate)
{
    if (*first == LSM_OK && candidate != LSM_OK)
        *first = candidate;
}

/* uname(2): kernel name/release/version, hostname, machine architecture. */
static lsm_status_t collect_uname(lsm_system_info_t *info)
{
    struct utsname u;
    if (uname(&u) != 0) {
        lsm_log_errno(LSM_TAG, "uname", errno);
        lsm_strlcpy(info->hostname, "N/A", sizeof(info->hostname));
        lsm_strlcpy(info->kernel_name, "N/A", sizeof(info->kernel_name));
        lsm_strlcpy(info->kernel_release, "N/A", sizeof(info->kernel_release));
        lsm_strlcpy(info->kernel_version, "N/A", sizeof(info->kernel_version));
        lsm_strlcpy(info->architecture, "N/A", sizeof(info->architecture));
        return LSM_ERR_IO;
    }

    lsm_strlcpy(info->hostname, u.nodename, sizeof(info->hostname));
    lsm_strlcpy(info->kernel_name, u.sysname, sizeof(info->kernel_name));
    lsm_strlcpy(info->kernel_release, u.release, sizeof(info->kernel_release));
    lsm_strlcpy(info->kernel_version, u.version, sizeof(info->kernel_version));
    lsm_strlcpy(info->architecture, u.machine, sizeof(info->architecture));
    return LSM_OK;
}

/*
 * /etc/os-release: shell-style KEY=VALUE lines, values optionally wrapped
 * in double or single quotes (per the freedesktop.org os-release spec).
 * We only need ID and PRETTY_NAME; a minimal line-oriented parser is
 * sufficient and avoids pulling in a full shell-quoting parser.
 */
static void strip_quotes(char *value)
{
    size_t len = strlen(value);
    if (len >= 2 &&
        ((value[0] == '"' && value[len - 1] == '"') ||
         (value[0] == '\'' && value[len - 1] == '\''))) {
        memmove(value, value + 1, len - 2);
        value[len - 2] = '\0';
    }
}

static lsm_status_t collect_os_release(lsm_system_info_t *info)
{
    lsm_strlcpy(info->os_id, "unknown", sizeof(info->os_id));
    lsm_strlcpy(info->os_pretty_name, "N/A", sizeof(info->os_pretty_name));

    char buf[4096];
    lsm_status_t status = lsm_read_file("/etc/os-release", buf, sizeof(buf), NULL);
    if (status != LSM_OK) {
        /* Some minimal/embedded systems ship it only under /usr/lib. */
        status = lsm_read_file("/usr/lib/os-release", buf, sizeof(buf), NULL);
        if (status != LSM_OK)
            return status;
    }

    int found_id = 0, found_pretty = 0;
    char *saveptr = NULL;
    char *line = strtok_r(buf, "\n", &saveptr);
    while (line != NULL) {
        char *eq = strchr(line, '=');
        if (eq != NULL) {
            *eq = '\0';
            char *key = lsm_str_trim(line);
            char *value = lsm_str_trim(eq + 1);
            strip_quotes(value);

            if (strcmp(key, "ID") == 0) {
                lsm_strlcpy(info->os_id, value, sizeof(info->os_id));
                found_id = 1;
            } else if (strcmp(key, "PRETTY_NAME") == 0) {
                lsm_strlcpy(info->os_pretty_name, value, sizeof(info->os_pretty_name));
                found_pretty = 1;
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    if (!found_id || !found_pretty)
        return LSM_ERR_PARSE;
    return LSM_OK;
}

/*
 * /proc/cpuinfo: per-logical-CPU block of "key\t: value" lines, blocks
 * separated by a blank line. The "model name" field is x86-specific; other
 * architectures expose different keys (e.g. "Hardware", "cpu model" on
 * some ARM/MIPS kernels) or none at all. We only read the first match
 * since every logical CPU on a homogeneous system reports the same model;
 * on genuinely heterogeneous SoCs (big.LITTLE) this under-reports, which
 * is an acceptable Phase 1 simplification.
 */
static lsm_status_t collect_cpu_model(lsm_system_info_t *info)
{
    lsm_strlcpy(info->cpu_model, "Unknown", sizeof(info->cpu_model));

    char buf[8192];
    lsm_status_t status = lsm_read_file("/proc/cpuinfo", buf, sizeof(buf), NULL);
    if (status != LSM_OK)
        return status;

    static const char *candidate_keys[] = {"model name", "Hardware", "cpu model", NULL};

    char *saveptr = NULL;
    char *line = strtok_r(buf, "\n", &saveptr);
    while (line != NULL) {
        char *colon = strchr(line, ':');
        if (colon != NULL) {
            char key_buf[64];
            size_t key_len = (size_t)(colon - line);
            if (key_len >= sizeof(key_buf))
                key_len = sizeof(key_buf) - 1;
            memcpy(key_buf, line, key_len);
            key_buf[key_len] = '\0';
            char *key = lsm_str_trim(key_buf);

            for (int i = 0; candidate_keys[i] != NULL; i++) {
                if (strcmp(key, candidate_keys[i]) == 0) {
                    char *value = lsm_str_trim(colon + 1);
                    lsm_strlcpy(info->cpu_model, value, sizeof(info->cpu_model));
                    return LSM_OK;
                }
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    return LSM_ERR_UNSUPPORTED;
}

/* sysconf(_SC_NPROCESSORS_ONLN): number of logical CPUs currently online
 * (excludes hot-plugged-off cores, unlike _SC_NPROCESSORS_CONF). This is
 * the figure that matches what the scheduler can actually use right now. */
static lsm_status_t collect_cpu_count(lsm_system_info_t *info)
{
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    if (count < 1) {
        lsm_log_errno(LSM_TAG, "sysconf(_SC_NPROCESSORS_ONLN)", errno);
        info->cpu_logical_count = 0;
        return LSM_ERR_IO;
    }
    info->cpu_logical_count = count;
    return LSM_OK;
}

/*
 * /proc/meminfo: "MemTotal:     16341236 kB". Reported in KiB despite the
 * "kB" label (kernel historical naming, see Documentation/filesystems/proc.rst).
 */
static lsm_status_t collect_ram_total(lsm_system_info_t *info)
{
    info->ram_total_kib = 0;

    char buf[4096];
    lsm_status_t status = lsm_read_file("/proc/meminfo", buf, sizeof(buf), NULL);
    if (status != LSM_OK)
        return status;

    char *saveptr = NULL;
    char *line = strtok_r(buf, "\n", &saveptr);
    while (line != NULL) {
        if (lsm_str_has_prefix(line, "MemTotal:")) {
            unsigned long long value = 0;
            if (sscanf(line, "MemTotal: %llu kB", &value) == 1) {
                info->ram_total_kib = value;
                return LSM_OK;
            }
            return LSM_ERR_PARSE;
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    return LSM_ERR_PARSE;
}

/* /proc/uptime: "<uptime_seconds> <idle_seconds_summed_over_cores>". */
static lsm_status_t collect_uptime(lsm_system_info_t *info)
{
    info->uptime_seconds = 0.0;

    char buf[64];
    lsm_status_t status = lsm_read_file("/proc/uptime", buf, sizeof(buf), NULL);
    if (status != LSM_OK)
        return status;

    char *end = NULL;
    double value = strtod(buf, &end);
    if (end == buf)
        return LSM_ERR_PARSE;

    info->uptime_seconds = value;
    return LSM_OK;
}

lsm_status_t lsm_sysinfo_collect(lsm_system_info_t *info)
{
    if (info == NULL)
        return LSM_ERR_INVALID_ARG;

    memset(info, 0, sizeof(*info));

    lsm_status_t first_failure = LSM_OK;

    set_first_failure(&first_failure, collect_uname(info));
    set_first_failure(&first_failure, collect_os_release(info));
    set_first_failure(&first_failure, collect_cpu_model(info));
    set_first_failure(&first_failure, collect_cpu_count(info));
    set_first_failure(&first_failure, collect_ram_total(info));
    set_first_failure(&first_failure, collect_uptime(info));

    return first_failure;
}
