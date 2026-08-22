#include "process/process.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

#include "core/log.h"
#include "utils/fileutils.h"
#include "utils/strutils.h"

#define LSM_TAG "process"

static lsm_status_t errno_to_status(int err)
{
    switch (err) {
    case ENOENT:
    case ESRCH:  return LSM_ERR_TRANSIENT;
    case EACCES:
    case EPERM:  return LSM_ERR_PERM;
    default:     return LSM_ERR_IO;
    }
}

/*
 * Returns a pointer to the start of the `n`-th space-separated token
 * (0-indexed) in `s`, or NULL if there are fewer than n+1 tokens.
 */
static const char *nth_token(const char *s, int n)
{
    for (int i = 0; i < n; i++) {
        s = strchr(s, ' ');
        if (s == NULL)
            return NULL;
        s++;
    }
    return s;
}

/*
 * Parses /proc/<pid>/stat. The comm field (2nd, in parentheses) can
 * contain arbitrary bytes including spaces and, in principle, ')'
 * itself (a process can rename itself via prctl(PR_SET_NAME) to nearly
 * anything within 15 bytes). The robust technique (used by procps/htop)
 * is to locate the *last* ')' in the line: every field after comm is
 * numeric or a single non-')' character, so the true closing paren is
 * guaranteed to be the rightmost one.
 *
 * Fields from `state` onward are addressed by 0-indexed position (proc(5)
 * field numbers 3-22, renumbered here starting at 0 = state):
 *   0 state  1 ppid  2 pgrp  3 session  4 tty_nr  5 tpgid  6 flags
 *   7 minflt 8 cminflt 9 majflt 10 cmajflt 11 utime 12 stime 13 cutime
 *   14 cstime 15 priority 16 nice 17 num_threads 18 itrealvalue 19 starttime
 * Only state, ppid, utime, stime and starttime are actually needed here.
 */
static lsm_status_t parse_stat_file(const char *buf, lsm_process_info_t *out)
{
    const char *open_paren = strchr(buf, '(');
    const char *close_paren = strrchr(buf, ')');
    if (open_paren == NULL || close_paren == NULL || close_paren < open_paren)
        return LSM_ERR_PARSE;

    size_t name_len = (size_t)(close_paren - open_paren - 1);
    if (name_len >= sizeof(out->name))
        name_len = sizeof(out->name) - 1;
    memcpy(out->name, open_paren + 1, name_len);
    out->name[name_len] = '\0';

    /* Everything after "<pid> (comm) " — fields are space-separated. */
    const char *rest = close_paren + 1;
    while (*rest == ' ')
        rest++;

    const char *state_tok = nth_token(rest, 0);
    const char *ppid_tok = nth_token(rest, 1);
    const char *utime_tok = nth_token(rest, 11);
    const char *stime_tok = nth_token(rest, 12);
    const char *nice_tok = nth_token(rest, 16);
    const char *starttime_tok = nth_token(rest, 19);

    if (state_tok == NULL || ppid_tok == NULL || utime_tok == NULL ||
        stime_tok == NULL || nice_tok == NULL || starttime_tok == NULL)
        return LSM_ERR_PARSE;

    out->state = *state_tok;
    out->ppid = (pid_t)strtol(ppid_tok, NULL, 10);
    unsigned long long utime = strtoull(utime_tok, NULL, 10);
    unsigned long long stime = strtoull(stime_tok, NULL, 10);
    out->cpu_time_ticks = utime + stime;
    out->nice_value = (int)strtol(nice_tok, NULL, 10);
    out->starttime_ticks = strtoull(starttime_tok, NULL, 10);
    return LSM_OK;
}

/* /proc/<pid>/status: "Uid:\t<real>\t<eff>\t<saved>\t<fs>", "Threads:\tN",
 * "VmRSS:\t   1234 kB" (VmRSS is absent for zombies/kernel threads — that
 * is not an error, it means 0 resident memory). */
static void parse_status_file(const char *buf, lsm_process_info_t *out)
{
    out->uid = (uid_t)-1;
    out->num_threads = 0;
    out->rss_kib = 0;

    char line_buf[4096];
    lsm_strlcpy(line_buf, buf, sizeof(line_buf));

    char *saveptr = NULL;
    char *line = strtok_r(line_buf, "\n", &saveptr);
    while (line != NULL) {
        unsigned long long value = 0;
        if (lsm_str_has_prefix(line, "Uid:")) {
            unsigned long real_uid = 0;
            if (sscanf(line + 4, "%lu", &real_uid) == 1)
                out->uid = (uid_t)real_uid;
        } else if (lsm_str_has_prefix(line, "Threads:")) {
            long threads = 0;
            if (sscanf(line + 8, "%ld", &threads) == 1)
                out->num_threads = threads;
        } else if (lsm_str_has_prefix(line, "VmRSS:")) {
            if (sscanf(line + 6, "%llu", &value) == 1)
                out->rss_kib = value;
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
}

/* /proc/<pid>/cmdline: NUL-separated argv, terminated by an extra NUL (or
 * simply end-of-file). Kernel threads and some zombies report it empty;
 * we then fall back to "[name]", matching ps(1)'s convention for
 * processes with no user-space command line. */
static void build_cmdline(const char *raw, size_t raw_len, const char *name,
                            char *out, size_t out_size)
{
    if (raw_len == 0) {
        snprintf(out, out_size, "[%s]", name);
        return;
    }

    size_t out_pos = 0;
    for (size_t i = 0; i < raw_len && out_pos + 1 < out_size; i++) {
        char c = raw[i];
        out[out_pos++] = (c == '\0') ? ' ' : c;
    }
    out[out_pos] = '\0';

    /* Trim a single trailing space left by the NUL-terminated last arg. */
    if (out_pos > 0 && out[out_pos - 1] == ' ')
        out[out_pos - 1] = '\0';
}

lsm_status_t lsm_process_read(pid_t pid, lsm_process_info_t *out)
{
    if (out == NULL || pid <= 0)
        return LSM_ERR_INVALID_ARG;

    memset(out, 0, sizeof(*out));
    out->pid = pid;

    char path[64];
    char buf[2048];

    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    lsm_status_t status = lsm_read_file(path, buf, sizeof(buf), NULL);
    if (status != LSM_OK)
        return status;
    status = parse_stat_file(buf, out);
    if (status != LSM_OK)
        return status;

    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    status = lsm_read_file(path, buf, sizeof(buf), NULL);
    if (status != LSM_OK) {
        /* The process could have exited between the two reads above —
         * report it the same way as any other TOCTOU disappearance. */
        return status;
    }
    parse_status_file(buf, out);

    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    size_t cmdline_len = 0;
    char cmdline_raw[1024];
    status = lsm_read_file(path, cmdline_raw, sizeof(cmdline_raw), &cmdline_len);
    if (status != LSM_OK && status != LSM_ERR_TRANSIENT && status != LSM_ERR_NOT_FOUND)
        return status;
    if (status != LSM_OK)
        cmdline_len = 0; /* process gone by this point: fall back gracefully */
    build_cmdline(cmdline_raw, cmdline_len, out->name, out->cmdline, sizeof(out->cmdline));

    return LSM_OK;
}

lsm_status_t lsm_process_list_collect(lsm_process_list_t *list)
{
    if (list == NULL)
        return LSM_ERR_INVALID_ARG;

    lsm_process_list_free(list);

    DIR *dir = opendir("/proc");
    if (dir == NULL) {
        lsm_log_errno(LSM_TAG, "/proc", errno);
        return errno_to_status(errno);
    }

    size_t capacity = 256;
    lsm_process_info_t *items = malloc(capacity * sizeof(*items));
    if (items == NULL) {
        closedir(dir);
        return LSM_ERR_NOMEM;
    }

    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        /* /proc contains many non-PID entries (self, cpuinfo, stat, ...);
         * a PID directory name is purely decimal digits. */
        const char *p = entry->d_name;
        if (*p == '\0' || !isdigit((unsigned char)*p))
            continue;
        int all_digits = 1;
        for (const char *q = p; *q != '\0'; q++) {
            if (!isdigit((unsigned char)*q)) {
                all_digits = 0;
                break;
            }
        }
        if (!all_digits)
            continue;

        pid_t pid = (pid_t)strtol(p, NULL, 10);

        if (count == capacity) {
            size_t new_capacity = capacity * 2;
            lsm_process_info_t *grown = realloc(items, new_capacity * sizeof(*items));
            if (grown == NULL) {
                free(items);
                closedir(dir);
                return LSM_ERR_NOMEM;
            }
            items = grown;
            capacity = new_capacity;
        }

        lsm_status_t status = lsm_process_read(pid, &items[count]);
        if (status == LSM_OK) {
            count++;
        } else if (status != LSM_ERR_TRANSIENT && status != LSM_ERR_NOT_FOUND &&
                   status != LSM_ERR_PERM) {
            LSM_LOGW(LSM_TAG, "failed to read pid %d: %s", pid, lsm_status_str(status));
        }
        /* TRANSIENT/NOT_FOUND (process vanished between readdir() and our
         * open()s) and PERM (e.g. another user's process without our
         * being able to read /proc/<pid>/status) are expected, routine
         * occurrences — silently skipped, not logged as warnings. */
    }

    closedir(dir);

    list->items = items;
    list->count = count;
    list->capacity = capacity;
    return LSM_OK;
}

void lsm_process_list_free(lsm_process_list_t *list)
{
    if (list == NULL)
        return;
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

lsm_status_t lsm_process_signal(pid_t pid, int sig)
{
    if (pid <= 0)
        return LSM_ERR_INVALID_ARG;

    if (kill(pid, sig) != 0)
        return errno_to_status(errno);
    return LSM_OK;
}

lsm_status_t lsm_process_renice(pid_t pid, int niceness)
{
    if (pid <= 0)
        return LSM_ERR_INVALID_ARG;

    errno = 0;
    if (setpriority(PRIO_PROCESS, (id_t)pid, niceness) != 0 && errno != 0)
        return errno_to_status(errno);
    return LSM_OK;
}
