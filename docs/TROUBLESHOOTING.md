# Troubleshooting

## `OS` line shows "N/A (unknown)"

`/etc/os-release` (and the `/usr/lib/os-release` fallback) is missing or
unreadable. This is expected on some minimal containers/chroots. Not a
bug — this is the documented graceful-degradation path
(`collect_os_release()` in `src/system/sysinfo.c`).

## `CPU model` shows "Unknown"

`/proc/cpuinfo` did not contain any of the recognized keys (`model name`,
`Hardware`, `cpu model`). Expected on some non-x86/non-ARM architectures
or unusual kernels. `lsm_sysinfo_collect()` still returns the rest of the
struct populated; only this field falls back.

## Build fails with "unknown type name" from `<sys/utsname.h>` or similar

Make sure you're not stripping `_DEFAULT_SOURCE` — some IDE-injected
compile flags override the Makefile's feature-test macros. Build via
`make`, not a hand-rolled `gcc` invocation, unless you replicate
`CFLAGS_COMMON` from the Makefile.

## `make test` fails only in a container / CI sandbox

`test_sysinfo.c`'s `test_collect_populates_fields` asserts
`ram_total_kib > 0` and `cpu_logical_count >= 1`, both of which require a
real (or realistically emulated) `/proc`. If you're running in an
environment without a real `/proc` (e.g. certain restricted sandboxes),
these will legitimately fail — this indicates the environment, not the
code, lacks the expected Linux interfaces.

## `linux-system-manager --daemon` says the daemon isn't running

Either `linux-system-managerd` genuinely isn't started, or a stale
socket file is left over from a previous instance that didn't shut down
cleanly (killed with `SIGKILL`, which the daemon cannot catch to run its
cleanup path). Start the daemon (`./bin/linux-system-managerd &` for a
quick manual check, or via the systemd unit below) — it unlinks and
recreates the socket file on startup, so a stale file alone is not a
blocker.

## The `systemd --user` unit crash-loops with "Read-only file system"

If you've edited `config/linux-system-managerd.service`'s hardening
directives, make sure `ReadWritePaths=%t` stays present alongside
`ProtectSystem=strict`. Without it, systemd's sandbox makes
`$XDG_RUNTIME_DIR` — where the daemon's socket lives — read-only inside
the service's own mount namespace, and `bind(2)` fails with `EROFS`.
This was found by actually running the shipped unit under
`systemctl --user start` (see docs/ARCHITECTURE.md's Phase 10 note);
`systemd-analyze verify` does not catch this class of problem, so always
test a hardening change against a real `systemctl --user start`, not
just the linter.

## ASan reports a leak/error only under `make debug`, not `make`/`make release`

That's the point of `make debug` — the dev/release builds don't link the
sanitizer runtime. Always validate with `make debug` before considering a
change done; see `docs/DEVELOPMENT.md`.
