#ifndef LSM_PROCESS_PROCESS_H
#define LSM_PROCESS_PROCESS_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "core/status.h"

/*
 * Phase 4 — Process Manager.
 *
 * Sources: /proc/<pid>/stat (pid/ppid/name/state/cpu ticks), /proc/<pid>/status
 * (real UID, thread count, RSS), /proc/<pid>/cmdline (argv), kill(2),
 * setpriority(2).
 *
 * TOCTOU is a first-class concern here, not an edge case: the set of PIDs
 * under /proc is inherently racy — a process enumerated by readdir() can
 * exit before we open() its /proc/<pid>/ files. lsm_process_read()
 * reports this as LSM_ERR_TRANSIENT and lsm_process_list_collect() simply
 * skips such entries rather than failing the whole listing, exactly as
 * required by the project's defensive-/proc-access rule.
 *
 * CPU usage: like the system-wide CPU monitor, /proc/<pid>/stat only
 * exposes cumulative utime+stime (in the same USER_HZ jiffies as
 * /proc/stat). This module deliberately does NOT compute a %CPU itself —
 * doing so correctly requires normalizing against a system-wide tick
 * delta (see cpu/cpu.h's lsm_cpu_times_total_ticks()), which is a
 * cross-module composition that belongs at the application layer
 * (main.c today; the daemon's collection loop later), not inside a
 * single-responsibility collector. This module exposes the raw
 * utime+stime tick counts; the caller diffs two samples itself.
 */
typedef struct lsm_process_info {
    pid_t pid;
    pid_t ppid;
    char name[32];          /* /proc/<pid>/stat "comm" field, kernel-capped at 15 bytes */
    char state;             /* R/S/D/Z/T/t/X/... , see proc(5) */
    uid_t uid;               /* real UID, from /proc/<pid>/status "Uid:" first field */
    long num_threads;        /* /proc/<pid>/status "Threads:" */
    uint64_t rss_kib;         /* /proc/<pid>/status "VmRSS:", 0 for zombies/kernel threads */
    int nice_value;            /* /proc/<pid>/stat "nice" field, -20..19; callers computing
                                 * a *relative* renice (e.g. the TUI's +/-1 keys) must read
                                 * this first — lsm_process_renice() sets an absolute value */
    unsigned long long cpu_time_ticks; /* utime + stime, USER_HZ jiffies since process start */
    unsigned long long starttime_ticks; /* process start time, USER_HZ jiffies since boot */
    char cmdline[512];        /* argv joined by spaces; "[name]" for kernel threads
                                * (empty cmdline); truncated with no partial-UTF8
                                * repair if the real command line is longer */
} lsm_process_info_t;

/*
 * Reads a single process's info by PID. Returns LSM_ERR_TRANSIENT if the
 * process exits during the read (distinguishing "gone mid-read" from
 * "never existed" is not reliable across the multiple files read here,
 * so both ENOENT-at-any-step cases are reported this way — callers
 * scanning /proc should treat LSM_ERR_TRANSIENT as "skip, not an error").
 */
lsm_status_t lsm_process_read(pid_t pid, lsm_process_info_t *out);

typedef struct lsm_process_list {
    lsm_process_info_t *items; /* heap-allocated, owned by this struct */
    size_t count;
    size_t capacity;
} lsm_process_list_t;

/*
 * Enumerates every process currently visible under /proc (i.e. every
 * process this UID has at least directory-read access to) into `list`.
 * `list` must be zero-initialized (or previously lsm_process_list_free'd)
 * before calling. Individual processes that vanish mid-scan are silently
 * skipped — see the TOCTOU note above. Growable heap array: process count
 * is genuinely unbounded, unlike the fixed-size per-core CPU arrays.
 */
lsm_status_t lsm_process_list_collect(lsm_process_list_t *list);

/* Releases the array owned by `list` and zeroes it. Safe to call twice. */
void lsm_process_list_free(lsm_process_list_t *list);

/*
 * Sends POSIX signal `sig` to `pid` (thin, validated wrapper over
 * kill(2)). `sig` 0 is the standard "does this process exist and am I
 * allowed to signal it" no-op check (see kill(2)).
 * Maps ESRCH -> LSM_ERR_NOT_FOUND, EPERM -> LSM_ERR_PERM.
 */
lsm_status_t lsm_process_signal(pid_t pid, int sig);

/*
 * Sets the nice value (scheduling priority, -20..19, lower = higher
 * priority) of `pid` via setpriority(2). Raising priority (lower value)
 * on a process you don't own requires CAP_SYS_NICE; this function does
 * not elevate privileges itself — it reports whatever the kernel decides
 * (EPERM/EACCES -> LSM_ERR_PERM), in keeping with the project's
 * least-privilege rule: this tool never runs a privileged helper or setuid
 * binary to bypass that check.
 */
lsm_status_t lsm_process_renice(pid_t pid, int niceness);

#endif /* LSM_PROCESS_PROCESS_H */
