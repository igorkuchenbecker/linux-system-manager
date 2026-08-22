# Development Guide

## Adding a new collector module (the pattern every phase follows)

1. Add `include/<domain>/<name>.h` with a struct describing the snapshot
   and one `lsm_status_t lsm_<domain>_collect(...)` entry point. Document,
   per field, which Linux interface it comes from.
2. Add `src/<domain>/<name>.c` implementing it. Structure it as one static
   `collect_<subfield>()` helper per data source, composed by the public
   `lsm_<domain>_collect()` — see `src/system/sysinfo.c` for the reference
   shape. Each helper must:
   - never crash on a missing/malformed file — return an `lsm_status_t`
     and leave its output field(s) at a safe fallback;
   - go through `utils/fileutils.h` for any `/proc` or `/sys` read, never
     raw `fopen`/`open`.
3. Add `tests/test_<name>.c`: at minimum, a smoke test against the real
   running system (fields are non-empty/non-negative/plausible) plus any
   pure-logic edge cases (parsing, formatting) using synthetic input.
4. Wire it into `src/main.c` (or, once it exists, the TUI) for visible
   output.
5. `make debug && make test` before considering the module done.

## Code style

- No comments explaining *what* code does — names should already say
  that. Comments are reserved for *why* (a kernel quirk, a TOCTOU
  hazard, a spec citation).
- `goto cleanup;` is the sanctioned pattern for multi-resource cleanup in
  C (no RAII). Not needed yet (Phase 1 has no multi-resource functions)
  but expected from Phase 4 (process scanning: fd + DIR* pairs) onward.
- Prefer fixed-size stack buffers with explicit capacity + `lsm_strlcpy`
  over heap allocation for anything bounded and short-lived (hostnames,
  single `/proc` lines). Reach for heap allocation only when size is
  genuinely unbounded (e.g. an arbitrary-length process list).
- Every `/proc`/`/sys` read goes through `utils/fileutils.h`. Do not add a
  second way to read a pseudo-file.

## Known simplifications (revisit later, not bugs)

- `collect_cpu_model()` (Phase 1) reads only the *first* `model name`
  entry in `/proc/cpuinfo`. On a heterogeneous SoC (big.LITTLE / hybrid
  x86) this misreports secondary core types. Acceptable for Phase 1;
  Phase 2's per-core work will likely want per-core model data anyway.
- `main.c` currently both collects and prints. This is fine for a
  single-module CLI; Phase 9 (TUI) is exactly the point at which
  presentation must be split from `main()`'s orchestration role.

## Running a single test binary directly

```sh
make test                       # builds everything under build/test/
./build/test/test_sysinfo       # run just one
```

## Debugging with GDB

```sh
make debug
gdb ./bin/linux-system-manager
```

Since the debug build already includes `-fsanitize=address,undefined`,
GDB will break automatically at the first sanitizer-detected violation
with a usable backtrace (`bt`).
