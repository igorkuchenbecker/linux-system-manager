# Architecture Overview

## Layering

```
Hardware
   ↓
Kernel / Drivers
   ↓
Linux Interfaces        /proc, /sys, /dev, syscalls, netlink, ioctl
   ↓
Data Collection Layer    src/<domain>/*.c  (cpu, memory, process, disk,
                          network, sensors, gpu, system)
   ↓
Core / Processing        src/core/*.c  (status codes, logging, later:
                          diffing, aggregation, sampling)
   ↓
Application Services     later: daemon, IPC server/client
   ↓
Presentation              src/main.c (CLI today) → src/ui/* (ncurses TUI,
                          Phase 9)
```

Each collector module (`src/cpu`, `src/memory`, ...) depends only on
`core/` and `utils/`. It never depends on `ui/`. This is enforced by
convention (reviewed at each phase) rather than by a build-time boundary,
since C has no module visibility system — the include-path discipline
(`#include "domain/header.h"`, one directory per domain) is what keeps
this legible.

## Module responsibilities

| Module      | Responsibility                                              | Primary Linux interfaces |
|-------------|---------------------------------------------------------------|---------------------------|
| `core`      | Status codes, logging. No knowledge of hardware or /proc.     | — |
| `utils`     | Generic, reusable helpers: safe file reads, string ops, formatting. Not domain-specific. | `open`/`read` |
| `system`    | Static/near-static machine identity (Phase 1).                 | `uname(2)`, `sysconf(3)`, `/etc/os-release`, `/proc/cpuinfo`, `/proc/uptime`, `/proc/meminfo` |
| `cpu`       | CPU usage, per-core stats, load, frequency (Phase 2, done).      | `/proc/stat`, `/proc/loadavg`, `/sys/devices/system/cpu/` |
| `memory`    | RAM/swap accounting (Phase 3, done).                             | `/proc/meminfo` |
| `process`   | Per-process discovery and control (Phase 4, done).               | `/proc/<pid>/`, `kill(2)`, `setpriority(2)` |
| `disk`      | Filesystem capacity/usage and block device throughput (Phase 5, done). | `/proc/mounts`, `/sys/block`, `statvfs(3)` |
| `network`   | Interface traffic counters (Phase 6, done).                      | `/proc/net/dev`, `/sys/class/net`, later netlink |
| `sensors`   | Temperature/fan/voltage/power (Phase 7, done).                    | `/sys/class/hwmon` |
| `gpu`       | Vendor-abstracted GPU metrics (Phase 8, done).                    | NVML (dlopen'd), `/sys/class/drm` (amdgpu, generic) |
| `ui`        | ncurses TUI (Phase 9, done). Renders data; never reads /proc directly. | ncursesw |
| `ipc`       | Wire protocol + client fetch (Phase 11, done). Shared by `linux-system-manager --daemon` and the daemon. | Unix domain sockets |
| `daemon`    | Background collector binary, `linux-system-managerd` (Phase 10, done). Its own `main()`, never linked into the CLI/TUI binary. | `epoll`, `timerfd`, `signalfd` |

`main.c` is currently the whole "application services + presentation"
layer: it calls `system/sysinfo.h` and prints to stdout. As phases add
modules, `main.c` grows into a thin composition point, and eventually
the daemon (Phase 10) takes over continuous collection while the CLI/TUI
become clients.

## Data flow (current, Phase 1-2)

```
main()
  -> lsm_sysinfo_collect(&info)
       -> collect_uname()        (uname(2))
       -> collect_os_release()   (/etc/os-release via lsm_read_file)
       -> collect_cpu_model()    (/proc/cpuinfo via lsm_read_file)
       -> collect_cpu_count()    (sysconf(_SC_NPROCESSORS_ONLN))
       -> collect_ram_total()    (/proc/meminfo via lsm_read_file)
       -> collect_uptime()       (/proc/uptime via lsm_read_file)
  -> print_system_info(&info)
  -> print_cpu_info()
       -> lsm_cpu_read_snapshot(&before)   (/proc/stat)
       -> nanosleep(300ms)
       -> lsm_cpu_read_snapshot(&after)    (/proc/stat)
       -> lsm_cpu_times_usage_percent(before, after)   (aggregate + per-core)
       -> lsm_cpu_load_average()           (/proc/loadavg)
       -> lsm_cpu_core_frequency_mhz(0)    (/sys/.../cpufreq/scaling_cur_freq)
```

Note on `lsm_cpu_times_usage_percent`: CPU usage is a rate, not an
instantaneous kernel value — the kernel only exposes cumulative jiffie
counters since boot. Two time-separated `/proc/stat` snapshots are
diffed; `main.c`'s 300ms sleep is a CLI-only convenience, not a library
requirement — a future daemon (Phase 10) will instead keep a rolling
previous snapshot and diff against it on each collection tick, with no
blocking sleep in the collection path.

Each `collect_*` helper is independent and never aborts the others: a
failure in one (missing file, parse error) sets a safe fallback for its
own fields and the first non-OK status is remembered only as a diagnostic
hint (see `core/status.h`). This is the concrete implementation of the
project rule "a missing sensor must not crash the program."

## TUI design (Phase 9)

`src/ui/` is split into `tui.c` (event loop, terminal setup/teardown,
input dispatch, header/footer chrome), `views_monitors.c` (the 8
read-only views: Dashboard/CPU/Memory/Disk/Network/Sensors/GPU/System),
and `views_process.c` (the Processes view, which additionally owns
selection/sort/kill/renice input handling). `include/ui/tui_internal.h`
holds the shared `lsm_tui_state_t` and view-function prototypes — it is
internal to `src/ui/`; nothing outside it may include this header.
`include/ui/tui.h` is the only public surface (`lsm_tui_run()`).

**Rolling-snapshot sampling.** The CLI (`main.c`) samples each
rate-based collector (CPU, process list, disk devices, network
interfaces) twice per section, blocking ~300ms in between — acceptable
for a one-shot snapshot, but it would freeze a live TUI for seconds on
every redraw if repeated per-section. The TUI instead keeps exactly one
rolling previous sample per rate-based collector in `lsm_tui_state_t`
and rotates curr→prev once per screen refresh tick (`lsm_tui_state_sample()`
in `tui.c`); the refresh interval itself (already needed for a
responsive UI, user-adjustable with `+`/`-`) is what used to be the
CLI's artificial sleep. This is the same "daemon-style" pattern flagged
as future work for the CPU module — it landed in the TUI first because
the TUI needed it first, not because Phase 10 arrived early.

**Two known, deliberately accepted simplifications:**
- The Processes view's %CPU column divides each process's tick delta by
  a hardcoded 100 (the near-universal Linux USER_HZ value) rather than
  `sysconf(_SC_CLK_TCK)`, for a lower-precision but always-available
  estimate; the CLI's process view (`main.c`) computes an exact figure by
  normalizing against the system-wide tick delta instead, which the TUI
  could equally do (tracked as a natural refinement, not implemented
  since the approximation is visually indistinguishable at 1Hz refresh).
- Sorting by "CPU" in the Processes view sorts by cumulative
  `cpu_time_ticks`, not the displayed instantaneous %CPU — correct
  relative order (higher cumulative usage tends to mean higher recent
  usage) but not identical to sorting by the displayed percentage column.

## Daemon + IPC design (Phase 10/11)

`linux-system-managerd` is a **separate binary** with its own `main()`
(`src/daemon/main.c`); the build system (`Makefile`) splits sources into
daemon-only, client-only, and shared, and compiles the shared collector
modules exactly once per build mode for both binaries — see the
Makefile's own header comment for why this split exists.

**Why `epoll`+`timerfd`+`signalfd`, not a thread.** The daemon has
exactly one writer of its in-memory cache (the timer-driven sampler) and
serves every client request synchronously from that same single-threaded
event loop, using data already in memory — there is no concurrent access
to guard, so no mutex, and therefore no argument for a second thread
(per this project's own rule: a thread needs a concrete architectural
justification, and none exists here). `signalfd` additionally sidesteps
the classic hazard of doing real work — logging, closing sockets — inside
an async-signal-handler context.

**Why the daemon's state struct is not shared with the TUI's.** Both
keep a rolling prev/curr sample per rate-based collector, but
`lsm_tui_state_t` (src/ui/tui_internal.h) exists to feed interactive
rendering with heap-owned growable lists, while the daemon's job is to
reduce every collector's output into one **fixed-size, IPC-serializable**
`lsm_ipc_snapshot_t` each tick. Forcing one struct to serve both would
couple a wire-format concern to a rendering concern for no real benefit
— see `src/daemon/state.h`'s header comment.

**Wire format** (`include/ipc/protocol.h`): a flat, naturally-aligned C
struct written as raw bytes over a Unix domain socket — deliberately
*not* a portable/versioned network format, because a Unix domain socket
cannot leave the local kernel, so both ends are always the same binary
on the same architecture. v0.1 scope excludes the live process list
(fundamentally unbounded, unlike every other collector here) from the
snapshot; a client needing it still calls `process/process.h` directly.
Protocol/struct-size fields let a client refuse a mismatched daemon
rather than misinterpret its bytes.

**DECISÃO REVISADA, found via live testing, not just compilation:**
the shipped `systemd --user` unit (`config/linux-system-managerd.service`)
initially set `ProtectSystem=strict` without `ReadWritePaths=%t` — this
passed `systemd-analyze verify` cleanly but made `$XDG_RUNTIME_DIR`
read-only *inside the service's own mount namespace*, so `bind(2)` on
the daemon's socket failed with EROFS and the service crash-looped under
`systemctl --user start`. Fixed by adding `ReadWritePaths=%t`. This is
exactly why this project validates hardening directives by actually
running the unit, not only linting it — `systemd-analyze verify` does
not catch this class of runtime-only sandboxing conflict. The same live
run also showed `statvfs()` failing with EACCES on systemd's
root-owned `/run/credentials/<service>` mounts specifically inside the
sandboxed daemon (but not in an unsandboxed process) — already handled
gracefully by `disk.c`'s per-mount skip, but it was logging at ERROR
severity for what is, in this environment, an expected/routine
condition; fixed to match `process.c`'s existing philosophy (EACCES/
EPERM/ENOENT on a per-item basis are silently skipped, not logged as
errors — only genuinely unexpected I/O failures are).

## Error handling strategy

- `lsm_status_t` (see `include/core/status.h`) is the return type for
  every fallible collector function. It is a *closed* enum — callers can
  switch over it exhaustively.
- Low-level POSIX failures (`errno`) are translated to `lsm_status_t` at
  the boundary (`utils/fileutils.c:errno_to_status`) and logged once via
  `lsm_log_errno()`; callers above that boundary work with
  `lsm_status_t`, not `errno`, so recovery logic never has to re-interpret
  raw error numbers.
- A struct-returning collector (e.g. `lsm_sysinfo_collect`) always leaves
  its output struct in a fully-defined state, even on error. Callers that
  ignore the returned status still get usable (if partially "N/A") data.
  This mirrors how `top`/`htop` degrade when a sensor is absent, rather
  than exiting.

## Logging strategy

`core/log.h` provides leveled logging (DEBUG/INFO/WARN/ERROR/FATAL) to
stderr, mutex-guarded for later multi-threaded collectors and the daemon.
stdout is reserved for actual program output (CLI data, eventually
machine-readable formats); logs never go to stdout so `lsm ... | jq` style
usage (future CLI/JSON output, see roadmap) stays uncontaminated.

## Testing strategy

- `tests/*.c`: one standalone test binary per unit under test, built and
  run in ASan+UBSan mode unconditionally (`make test`), so correctness
  and memory-safety are checked together.
- No external test framework dependency — `tests/test_common.h` is a
  ~15-line assert-and-exit harness. This keeps `make test` runnable with
  nothing beyond a C toolchain.
- Tests favor real Linux interfaces where they are guaranteed to exist
  (e.g. `/proc/uptime` is always readable on Linux) and synthetic
  temp files (`mkstemp`) where exact byte-level behavior (truncation,
  exact content) needs to be pinned down deterministically.
- Failure-path testing (nonexistent file → `LSM_ERR_NOT_FOUND`, NULL
  argument → `LSM_ERR_INVALID_ARG`) is treated as first-class, not an
  afterthought — per the project rule that `/proc`/`/sys` access must be
  defensive by default.

## Build strategy

See `docs/BUILD.md`. Summary: `make` (fast dev build), `make debug`
(ASan+UBSan), `make release` (`-O2 -DNDEBUG`), `make test`, `make clean`.
Each mode compiles into its own `build/<mode>/` tree to avoid mixing
objects built with incompatible flags.

## Directory structure

```
linux-system-manager/
├── include/<domain>/    public headers, one directory per domain
├── src/<domain>/        implementation, mirrors include/
├── tests/                flat: one test binary per unit
├── docs/                 architecture, build, dev, troubleshooting docs
├── scripts/               dev helper scripts (none yet)
├── config/                 runtime config files (none yet — Phase 10+)
├── Makefile
└── README.md
```

This mirrors the structure suggested by the project brief. No deviation
was needed for Phase 0/1; deviations will be called out explicitly if a
later phase's requirements make one necessary (e.g. Phase 11's IPC layer
may warrant a top-level `ipc/` domain shared by both the daemon and the
client).
