# Linux System Manager

A native Linux system/hardware monitor and process manager, written in C,
built directly on top of Linux kernel interfaces (`/proc`, `/sys`,
syscalls, POSIX APIs) rather than by shelling out to and parsing tools
like `top`/`free`/`lsblk`/`sensors`.

This is both a real utility and a systems-programming learning project:
each module is a self-contained exploration of one corner of how Linux
exposes hardware and process state to userspace.

## Status

**Phase 12 of 12 complete — v0.1 roadmap done** — see `docs/ARCHITECTURE.md` for the full
roadmap. Currently implemented:

- **Phase 0** — architecture, module layout, build system, error/logging
  strategy, testing strategy.
- **Phase 1** — System Information: hostname, kernel, architecture,
  distribution, CPU model, logical CPU count, total RAM, uptime.
- **Phase 2** — CPU Monitor: total and per-core usage percentage (via
  `/proc/stat` two-sample diffing), load average, per-core frequency.
- **Phase 3** — Memory Monitor: RAM total/used/available/buffers/cached
  and swap accounting via `/proc/meminfo`, with `used` deliberately
  derived from the kernel's own `MemAvailable` estimate rather than the
  naive (and misleading) `total - free`.
- **Phase 4** — Process Manager: per-process PID/PPID/name/state/UID/
  threads/RSS/cmdline via `/proc/<pid>/`, process listing sorted by RSS,
  %CPU composed from process + CPU snapshots, plus `kill`/`SIGSTOP`/
  `SIGCONT`-capable signaling and `renice` control functions.
- **Phase 5** — Disk Monitor: mounted filesystem capacity/usage via
  `/proc/mounts` + `statvfs(3)`, and block device read/write throughput
  via `/sys/block/<dev>/stat`.

- **Phase 6** — Network Monitor: per-interface RX/TX byte and packet
  counters via `/proc/net/dev`, operational state via
  `/sys/class/net/<if>/operstate`, throughput computed from two
  time-separated samples.

- **Phase 7** — Hardware Sensors: temperature/fan/voltage/power via
  `/sys/class/hwmon/`, tolerant of the huge variance in what real
  hardware actually exposes (an empty sensor list is a valid result, not
  an error).

- **Phase 8** — GPU: vendor-abstracted backends (NVIDIA via NVML,
  dlopen'd at runtime with no build-time dependency; AMD via amdgpu
  sysfs; Intel and unknown vendors via generic PCI-ID identification).
  Each backend reports only what it can actually obtain — no fabricated
  parity between vendors. Validated live against a real NVIDIA RTX 2060.

- **Phase 9** — TUI: full-screen ncurses dashboard with 9 views
  (Dashboard, CPU, Memory, Processes, Disk, Network, Sensors, GPU,
  System), live-updating on a configurable refresh interval, sortable/
  scrollable process list with interactive `kill`/`renice`
  (confirmation-gated). The CLI snapshot mode (`--cli`, or automatic
  fallback when stdout isn't a terminal) is unchanged and still available.

- **Phase 10** — Daemon (`linux-system-managerd`): continuous background
  collection on a `timerfd`, event-driven with `epoll` (no polling loop,
  no threads — a single event loop is sufficient since there is exactly
  one writer of the cache). Graceful shutdown via `signalfd` on
  SIGTERM/SIGINT. Ships a `systemd --user` unit
  (`config/linux-system-managerd.service`), least-privilege by
  construction (no root needed — reads only /proc, /sys, and a
  user-private Unix socket) and validated by actually running it under
  `systemctl --user` with sandboxing hardening enabled (`ProtectSystem=strict`
  and friends) — this caught and fixed a real EROFS crash-loop from the
  socket path needing an explicit `ReadWritePaths=%t`.
- **Phase 11** — IPC: a fixed-size, flat snapshot struct
  (`include/ipc/protocol.h`) sent over the daemon's Unix domain socket;
  `linux-system-manager --daemon` fetches and prints one, proving the
  round trip end to end (verified against a real running daemon,
  including under the hardened systemd unit above). The live process
  list is intentionally out of scope for v0.1's IPC payload — see the
  header's rationale; clients that need it still call
  `process/process.h` directly, as the CLI/TUI already do.

- **Phase 12** — Security: documented threat model and privilege
  boundaries (`docs/SECURITY.md`); an explicit, reasoned decision to use
  zero Linux capabilities (nothing in this project performs a privileged
  operation that would justify one); a startup privilege notice
  (`core/privilege.c`) logged by both binaries; confirmation that every
  `kill`/`renice` control path defers entirely to the kernel's own
  UID-based authorization rather than second-guessing or bypassing it.

See `docs/SECURITY.md` for the full write-up, and `docs/ARCHITECTURE.md`
for the complete module-by-module architecture and every design decision
made along the way (including the two bugs caught by actually running
the TUI and the daemon rather than only compiling them).

## Quick start

```sh
make                              # dev build (builds both binaries)
./bin/linux-system-manager        # launches the TUI (needs a real terminal)
./bin/linux-system-manager --cli  # one-shot text snapshot instead

./bin/linux-system-managerd &        # background collector daemon
./bin/linux-system-manager --daemon  # fetch one snapshot from it over IPC
```

### Installing the `linuxmng` command

```sh
make install      # release build + installs as ~/.local/bin/{linuxmng,linuxmngd}
linuxmng           # now runs from any directory, just like any other command
linuxmng --cli
linuxmngd &
linuxmng --daemon
```

`make install` defaults to `PREFIX=~/.local` (no root needed — this is
already on PATH on most modern distros, including the one this project
was developed on). For a system-wide install instead, run
`make install PREFIX=/usr/local` (needs `sudo`). `make uninstall` (same
`PREFIX`) removes what was installed.

Piped/redirected output (`./bin/linux-system-manager | less`, cron, CI)
automatically falls back to the `--cli` snapshot mode, since a TUI has no
meaning without a terminal to draw into.

TUI keys: `1`-`9` or `Tab` to switch views, `+`/`-` to change the refresh
rate, `q` to quit. In the Processes view: arrow keys to select, `c`/`m`
to sort by CPU/memory, `k`/`x` to send SIGTERM/SIGKILL (asks `y` to
confirm), `[`/`]` to renice.

Example output:

```
Linux System Manager — System Information
-------------------------------------------
Hostname       : nevermore
OS             : CachyOS (cachyos)
Kernel         : Linux 6.x.x-cachyos (x86_64)
CPU model      : AMD Ryzen 5 2600 Six-Core Processor
Logical CPUs   : 12
RAM total      : 15.6 GiB
Uptime         : 06:24:32

Linux System Manager — CPU Monitor
-------------------------------------------
Total usage    : 18.1%
Load average   : 1.79 1.68 1.46 (1/5/15 min)
Per-core usage : cpu0=27% cpu1=21% cpu2=14% ...
cpu0 frequency : 2061 MHz
```

## Documentation

- `docs/ARCHITECTURE.md` — layering, module responsibilities, data flow,
  error/logging/testing strategy.
- `docs/BUILD.md` — build modes, compiler flags, dependencies.
- `docs/DEVELOPMENT.md` — how to add a new collector module, code style,
  known simplifications.
- `docs/TROUBLESHOOTING.md` — common issues and their causes.
- `docs/SECURITY.md` — privilege model, UID/GID handling, why no
  capabilities are used, daemon sandboxing.

## Design principles

- **Native Linux interfaces first.** No shelling out to `top`, `free`,
  `df`, `lsblk`, `lspci`, `ps`, `ip`, or `sensors`. When an external tool
  is genuinely unavoidable, it will be called out explicitly in the code
  and docs.
- **Graceful degradation.** A missing sensor, unreadable `/proc` entry, or
  unsupported kernel feature must never crash the program — it reports
  "N/A" / a safe fallback and continues.
- **Modular, layered architecture.** Collectors know nothing about
  presentation; presentation knows nothing about `/proc` parsing. See
  `docs/ARCHITECTURE.md`.
- **C, POSIX, no heavy frameworks.** No microservices, no database, no
  containers — this is a systems tool, and complexity is spent only where
  it produces real technical value.

## License

See `LICENSE`.
