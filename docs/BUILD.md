# Build Guide

## Requirements

- GCC or Clang with C17 support (project developed against GCC 16 / Clang 22)
- GNU Make
- Linux headers (standard on any Linux dev machine)
- `libncursesw` (development headers) — required starting Phase 9 (the
  TUI); linked unconditionally as `-lncursesw`
- `libpthread` — linked unconditionally starting Phase 1 (`core/log.c`
  uses a mutex); every distro's glibc ships it
- `libdl` — linked unconditionally starting Phase 8 (the GPU module's
  NVIDIA backend uses `dlopen()`/`dlsym()`); every distro's glibc ships
  it too. `libnvidia-ml.so(.1)` itself is a *runtime-only* dependency:
  the binary never fails to build or run without it — the NVIDIA GPU
  backend simply reports zero devices (LSM_OK, empty list) when it is
  absent, which is the expected outcome on any non-NVIDIA machine.

No other third-party dependencies. No package manager integration
(vcpkg/conan/etc.) — deliberately, per the project's "no heavy frameworks
without justification" rule.

## Commands

```sh
make            # dev build: -g -O0, no sanitizers -> bin/linux-system-manager
                #            and bin/linux-system-managerd
make debug      # -g -O0 -fsanitize=address,undefined
make release    # -O2 -DNDEBUG
make test       # builds and runs tests/*.c (always ASan+UBSan)
make run        # build (dev) + execute the CLI/TUI binary
make run-daemon # build (dev) + execute the daemon in the foreground
make install    # release build + installs as ~/.local/bin/{linuxmng,linuxmngd}
make uninstall  # removes what `make install` placed
make clean      # removes build/ and bin/
```

`make install`/`make uninstall` take `PREFIX` (default `$HOME/.local`,
needs no root; pass `PREFIX=/usr/local` for a system-wide install, which
does need `sudo`). The installed command names (`linuxmng`, `linuxmngd`)
intentionally differ from the build artifact names in `bin/`
(`linux-system-manager`, `linux-system-managerd`) — the former is what a
user types, the latter is this project's internal/build identity.

Every mode builds **two** binaries: `linux-system-manager` (CLI/TUI,
`src/main.c` + `src/ui/`) and `linux-system-managerd` (the daemon,
`src/daemon/`). They share every collector/core/utils/ipc object file,
compiled once per mode — see the Makefile's own comments for how the
source-list split avoids a duplicate-`main()` link error.

Each mode builds into its own `build/<mode>/` object tree
(`build/dev`, `build/debug`, `build/release`, `build/test`), so switching
between modes never links stale objects built with different flags.
`bin/linux-system-manager` always reflects whichever mode you built most
recently.

## Compiler flags

Common to all modes:
`-std=c17 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Wall -Wextra
-Wshadow -Wpointer-arith -Wcast-align -Wwrite-strings
-Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition
-Wformat=2`

`_POSIX_C_SOURCE=200809L` (POSIX.1-2008) is deliberately used instead of
`_GNU_SOURCE`: it gives us `strtok_r`, `strerror_r` (XSI variant, which
returns `int` — the code relies on this), `getline`, `clock_gettime`, etc.
without pulling in the full GNU extension surface, keeping the codebase
closer to portable POSIX+Linux rather than glibc-specific. `_DEFAULT_SOURCE`
is added on top because glibc's `<sys/utsname.h>`/`<sys/types.h>` headers
expect it for some historical BSD-ish declarations; it does not change the
`strerror_r` variant selection.

`-Werror` is intentionally **not** enabled globally yet: the project is
early enough that a stray warning should be visible without hard-failing
CI-less local builds. It will be added once the build is CI-gated
(tracked informally; revisit at Phase 10).

## Verifying a build

```sh
make debug && ./bin/linux-system-manager   # should run clean under ASan/UBSan
make test                                   # all tests must print PASS and exit 0
```

If Valgrind is available:

```sh
valgrind --leak-check=full --error-exitcode=99 ./bin/linux-system-manager
```

(Not required — ASan already catches the same class of bugs — but useful
as a second opinion when hunting a specific leak.)
