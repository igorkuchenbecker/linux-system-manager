#ifndef LSM_UI_TUI_H
#define LSM_UI_TUI_H

#include "core/status.h"

/*
 * Phase 9 — TUI.
 *
 * Runs the full-screen ncurses dashboard until the user quits (`q`).
 * This is the only function outside src/ui/ needs to know about — the
 * UI layer never exposes ncurses types or its internal per-view
 * rendering functions (see ui/tui_internal.h, private to src/ui/).
 *
 * Per the project's layering rule, this module never reads /proc or
 * /sys directly: every value on screen comes from the same collector
 * APIs (cpu/, memory/, process/, disk/, network/, sensors/, gpu/,
 * system/) the CLI in main.c uses.
 *
 * Requires a real terminal (a TTY) — returns LSM_ERR_UNSUPPORTED without
 * touching the terminal at all if stdout is not one (e.g. piped output,
 * a non-interactive CI run), rather than letting ncurses fail deep
 * inside initscr().
 */
lsm_status_t lsm_tui_run(void);

#endif /* LSM_UI_TUI_H */
