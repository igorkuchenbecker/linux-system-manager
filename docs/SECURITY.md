# Security & Permissions (Phase 12)

## Summary

This project requires **no elevated privileges** to build, install, or
run for its primary purpose (monitoring your own session). No binary is
setuid/setgid. No Linux capability is requested, dropped, or restored.
Every privileged-*feeling* operation (`kill`, `renice`) is authorized
entirely by the kernel's own standard UID-based checks — this project
adds no privilege of its own and never attempts to work around a denial.

## Why no capabilities (libcap)

Linux capabilities exist to grant a *subset* of root's power to a
process that needs exactly one privileged operation (e.g. `CAP_NET_BIND_SERVICE`
to bind port 80 as a non-root user). This project has no such operation:

- Every read this project performs (`/proc`, `/sys`, `statvfs`) uses
  ordinary, unprivileged file access, gated by the kernel's normal
  permission checks on those pseudo-files.
- `kill(2)`/`setpriority(2)` (process/process.h) are called with the
  invoking user's own real privileges; the kernel decides whether the
  signal/renice is allowed (matching UID, or `CAP_KILL`/`CAP_SYS_NICE`
  the invoking process already legitimately has — e.g. because it's
  running as root for its own reasons). This project never elevates to
  make such a call succeed.

Writing capability-dropping code for a program with nothing privileged
to drop would be pure ceremony — this project's own anti-overengineering
rule (docs/DEVELOPMENT.md) applies directly here.

## UID/GID handling

- `process/process.c` reads the **real** UID (`/proc/<pid>/status`'s
  `Uid:` first field) for the `uid` shown per process — matching `ps`'s
  default convention, and the one meaningful for "who owns this
  process" regardless of any setuid execution that process itself may
  have done.
- `lsm_process_signal()`/`lsm_process_renice()` never inspect or compare
  UIDs themselves; they pass the request straight to the kernel and
  translate `EPERM`/`EACCES` to `LSM_ERR_PERM` (see core/status.h),
  which every caller (CLI, TUI) already surfaces as a plain "permission
  denied" message rather than crashing or retrying with elevated rights.
- `core/privilege.c`'s `lsm_log_privilege_notice()` logs the real/effective
  UID once at startup (both the CLI/TUI and the daemon call it) —
  informational only. Running as root is not blocked (it is occasionally
  useful: it grants visibility into other users' `/proc/<pid>/cmdline`
  and lets `kill`/`renice` target their processes), but this project
  never asks for it and works fully for the invoking user's own session
  without it.

## The daemon (`linux-system-managerd`)

- Designed to run as an ordinary user via `systemd --user`
  (`config/linux-system-managerd.service`), never as a system-wide root
  service — there is no `/etc/systemd/system` unit shipped, deliberately.
- Its Unix domain socket is created under `$XDG_RUNTIME_DIR` (already
  mode 0700, private to the user, when systemd manages the session) and
  explicitly `chmod`'d to 0600 as defense in depth for the `/tmp`
  fallback path used when `$XDG_RUNTIME_DIR` is unset — only the
  daemon's own UID can connect to it, so a snapshot cannot leak to
  another local user.
- The shipped systemd unit adds sandboxing on top of the (already
  unprivileged) process: `NoNewPrivileges`, `ProtectSystem=strict` +
  `ReadWritePaths=%t` (only the runtime directory is writable),
  `ProtectHome=read-only`, `PrivateTmp`, `RestrictAddressFamilies=AF_UNIX`
  (this daemon never touches the network — enforced, not just
  documented), `MemoryDenyWriteExecute`. Every directive here was
  validated against a real `systemctl --user start`, not only
  `systemd-analyze verify` — see docs/ARCHITECTURE.md's Phase 10 note for
  the EROFS crash-loop that lint-only checking missed.
- Known limitation: no PID-file locking, so nothing stops a second
  instance of the daemon from being started by the same user and
  re-binding the same socket path (the second one simply wins the bind).
  Not a cross-user concern (the runtime directory is already private per
  user) — just run one instance.

## What this project deliberately does NOT do

- No setuid/setgid binary, ever.
- No `sudo`/`pkexec` invocation from within the program.
- No writing outside of: the daemon's own runtime-directory socket, and
  whatever the user explicitly asks for (there is no config file or log
  file written by default in v0.1).
- No trusting of `/proc`/`/sys` content without validation — every
  collector treats these as adversarial-by-neglect (they can be racy,
  malformed, or absent) per docs/ARCHITECTURE.md and docs/DEVELOPMENT.md,
  which is itself a security property: a parser that assumes
  well-formed input is a parser that can be driven to undefined behavior
  by an unexpected value.
