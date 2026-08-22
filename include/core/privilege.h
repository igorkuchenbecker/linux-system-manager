#ifndef LSM_CORE_PRIVILEGE_H
#define LSM_CORE_PRIVILEGE_H

/*
 * Phase 12 — Security & Permissions.
 *
 * This project never requires root and never installs a setuid/setgid
 * binary: every data source it reads (/proc, /sys) is either
 * world-readable for the fields this project uses, or gracefully
 * degrades to "N/A"/skipped when a specific file is not readable by the
 * invoking user (see core/status.h's LSM_ERR_PERM and every collector's
 * handling of it). Process control (kill/renice, process/process.h)
 * relies entirely on the kernel's own permission checks — this project
 * adds no privilege of its own on top of what the invoking user already
 * has, and never attempts to bypass a denial (no setuid re-exec, no
 * capability-dropping-then-restoring dance).
 *
 * Running as root IS occasionally legitimate for this program (it
 * grants visibility into other users' /proc/<pid>/cmdline and similar
 * restricted fields, and lets `kill`/`renice` target other users'
 * processes) — this is advisory logging, not a restriction.
 */

/*
 * Logs one INFO-level line noting the current real/effective UID and,
 * if running as root (EUID 0), an explicit note that this project does
 * not require it for operating on the invoking user's own processes.
 * Call once near the start of main().
 */
void lsm_log_privilege_notice(const char *program_tag);

#endif /* LSM_CORE_PRIVILEGE_H */
