# Linux System Manager

A system monitor, hardware monitor, and process manager for Linux,
written in C. It reads `/proc`, `/sys`, and standard syscalls directly
instead of parsing the output of `top`, `free`, `lsblk`, or `sensors`.

Ships as three things: a terminal UI, a one-shot CLI snapshot, and an
optional background daemon that other clients can query over a Unix
socket.

## Features

- **System info** — hostname, kernel, distro, CPU model, RAM, uptime
- **CPU** — total and per-core usage, load average, per-core frequency
- **Memory** — RAM and swap, with a correct used/available split (not
  the naive "total minus free")
- **Processes** — list, sort by CPU or memory, `kill` / `SIGKILL` /
  renice, right from the TUI
- **Disk** — mounted filesystem capacity/usage and per-device read/write
  throughput
- **Network** — per-interface traffic and link state
- **Sensors** — temperature, fan speed, voltage, power, wherever the
  kernel exposes them
- **GPU** — NVIDIA (via NVML), AMD (via amdgpu sysfs), and basic
  identification for Intel/other vendors
- **Daemon + IPC** — `linuxmngd` samples everything in the background
  and serves it over a Unix socket, so a client doesn't have to re-scan
  `/proc` on every request

## Requirements

- Linux, x86_64
- GCC or Clang, GNU Make
- `ncursesw` (for the TUI)

Nothing else is required to build. The NVIDIA GPU backend loads
`libnvidia-ml.so` at runtime if it's present and simply reports no
NVIDIA GPU if it isn't — no build-time dependency either way.

## Build & install

```sh
make               # builds bin/linux-system-manager and bin/linux-system-managerd
make install       # installs as ~/.local/bin/linuxmng and linuxmngd (no root needed)
```

Once installed:

```sh
linuxmng           # opens the TUI
linuxmng --cli     # one-shot text snapshot instead
linuxmngd &        # start the background daemon
linuxmng --daemon  # fetch a snapshot from the daemon over IPC
```

`make install` defaults to `~/.local/bin`. For a system-wide install,
use `make install PREFIX=/usr/local` (needs `sudo`). `make uninstall`
removes it.

Redirected or piped output (`linuxmng | less`, cron, CI) automatically
falls back to the text snapshot, since a full-screen UI has nowhere to
draw.

## TUI controls

`1`-`9` or `Tab` to switch views, `+`/`-` to change the refresh rate,
`q` to quit. In the Processes view: arrow keys to select, `c`/`m` to
sort by CPU or memory, `k`/`x` to send SIGTERM/SIGKILL (confirmation
required), `[`/`]` to renice.

## Running the daemon as a service

A `systemd --user` unit is included:

```sh
mkdir -p ~/.config/systemd/user
cp config/linux-system-managerd.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now linux-system-managerd
```

It runs as your own user — no root, no system-wide service — and is
sandboxed (`ProtectSystem=strict`, no network access, etc.).

## Documentation

- `docs/ARCHITECTURE.md` — module layout, data flow, design rationale
- `docs/BUILD.md` — build modes and compiler flags
- `docs/DEVELOPMENT.md` — adding a new collector module, code style
- `docs/SECURITY.md` — privilege model and sandboxing
- `docs/TROUBLESHOOTING.md` — common issues

## Design notes

- No shelling out to other CLI tools for data — everything comes from
  `/proc`, `/sys`, or a direct syscall.
- A missing sensor, unreadable file, or unsupported feature degrades to
  "N/A" instead of crashing.
- Collectors, core logic, and the UI are separated: the TUI has no idea
  how `/proc/stat` is formatted, and the CPU module has no idea ncurses
  exists.

## License

MIT — see `LICENSE`.
