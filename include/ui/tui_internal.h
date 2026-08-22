#ifndef LSM_UI_TUI_INTERNAL_H
#define LSM_UI_TUI_INTERNAL_H

#include <ncurses.h>
#include <stddef.h>
#include <time.h>

#include "cpu/cpu.h"
#include "disk/disk.h"
#include "gpu/gpu.h"
#include "memory/memory.h"
#include "network/network.h"
#include "process/process.h"
#include "sensors/sensors.h"
#include "system/sysinfo.h"

/*
 * Internal to src/ui/ — shared between tui.c (event loop) and the view
 * renderers (views_monitors.c, views_process.c). Not part of the public
 * API; main.c only ever calls ui/tui.h's lsm_tui_run().
 *
 * DECISÃO REVISADA (Phase 9): the CLI's per-section sampling (main.c's
 * print_cpu_info() et al., each blocking ~300ms in nanosleep() to get a
 * two-sample diff) does not scale to a live TUI — eight sections * 300ms
 * would freeze the screen for ~2.4s on every single redraw. This module
 * instead keeps ONE rolling previous sample per rate-based collector
 * (CPU, process list, disk devices, network interfaces) in this struct
 * and diffs against it once per screen refresh tick; the refresh
 * interval itself (already needed for a responsive UI) is what used to
 * be main.c's artificial sleep. This is exactly the "daemon-style
 * rolling snapshot" pattern flagged as future work in
 * docs/ARCHITECTURE.md's CPU section — it arrived here first because the
 * TUI needs it, not the daemon (Phase 10) yet.
 */
typedef enum lsm_tui_view {
    LSM_VIEW_DASHBOARD = 0,
    LSM_VIEW_CPU,
    LSM_VIEW_MEMORY,
    LSM_VIEW_PROCESS,
    LSM_VIEW_DISK,
    LSM_VIEW_NETWORK,
    LSM_VIEW_SENSORS,
    LSM_VIEW_GPU,
    LSM_VIEW_SYSTEM,
    LSM_VIEW_COUNT,
} lsm_tui_view_t;

typedef struct lsm_tui_state {
    lsm_tui_view_t view;
    int refresh_ms;
    int running;

    lsm_system_info_t sysinfo; /* collected once at startup: static data */

    /*
     * Every rate-based collector below is sampled once per tick by
     * lsm_tui_state_sample(), which rotates curr into prev first. A
     * single shared counter (rather than one has_*_prev flag per
     * resource) tracks whether prev/curr form a valid diffable pair:
     * sample_count reaches 2 after the second tick, at which point every
     * "prev"/"curr" pair below is meaningful. Before that (the very
     * first frame), rate displays simply show 0 — the same harmless
     * "no data yet" first frame every sampling monitor (htop included)
     * shows on startup.
     */
    int sample_count;

    lsm_cpu_snapshot_t cpu_prev, cpu_curr;

    lsm_memory_info_t memory;

    lsm_process_list_t proc_prev, proc_curr;
    size_t proc_selected; /* index into the sorted proc_curr list */
    size_t proc_scroll;    /* first visible row, for scrolling long lists */
    int proc_sort_by_cpu;   /* 1 = sort by %CPU, 0 = sort by RSS */

    lsm_mount_list_t mounts;
    lsm_disk_device_list_t disk_prev, disk_curr;

    lsm_network_list_t net_prev, net_curr;

    lsm_sensor_list_t sensors;
    lsm_gpu_list_t gpus;

    struct timespec last_sample_time; /* timestamp of the most recent tick */
    double last_elapsed_seconds;       /* true wall-clock time since the
                                         * *previous* tick, measured in
                                         * lsm_tui_state_sample() before
                                         * last_sample_time is overwritten.
                                         * Disk/network KiB/s (unlike CPU%,
                                         * which is a tick-ratio and needs
                                         * no wall-clock time at all) are
                                         * divided by this, not by the
                                         * nominal refresh_ms — actual loop
                                         * time includes render+input work
                                         * too, so it never exactly equals
                                         * the requested refresh interval. */

    char status_message[160]; /* transient feedback, e.g. "Sent SIGTERM to 1234" */
    time_t status_message_expires_at;

    int pending_confirm_signal; /* 0 = none; else the signal awaiting 'y' confirm */
    pid_t pending_confirm_pid;
} lsm_tui_state_t;

/* One-time setup: takes the first sample of every collector so the
 * first frame has data instead of all-zero deltas. */
void lsm_tui_state_init(lsm_tui_state_t *state);

/* Releases every heap-owned list in `state`. */
void lsm_tui_state_destroy(lsm_tui_state_t *state);

/* Refreshes every collector for one tick, rotating prev<-curr for the
 * rate-based ones first. Call once per redraw. */
void lsm_tui_state_sample(lsm_tui_state_t *state);

void lsm_tui_set_status(lsm_tui_state_t *state, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* View renderers (views_monitors.c, views_process.c). Each draws into
 * stdscr starting at row `top_row`, stopping before `LINES - 1` (the
 * footer row) — see tui.c for the row budget. */
void lsm_tui_render_dashboard(const lsm_tui_state_t *state, int top_row);
void lsm_tui_render_cpu(const lsm_tui_state_t *state, int top_row);
void lsm_tui_render_memory(const lsm_tui_state_t *state, int top_row);
void lsm_tui_render_disk(const lsm_tui_state_t *state, int top_row);
void lsm_tui_render_network(const lsm_tui_state_t *state, int top_row);
void lsm_tui_render_sensors(const lsm_tui_state_t *state, int top_row);
void lsm_tui_render_gpu(const lsm_tui_state_t *state, int top_row);
void lsm_tui_render_system(const lsm_tui_state_t *state, int top_row);

/* The process view owns extra input handling (selection/sort/signal),
 * unlike the read-only monitor views, so it gets its own input entry
 * point in addition to its renderer. */
void lsm_tui_render_process(const lsm_tui_state_t *state, int top_row);

/*
 * Returns 1 if `ch` was consumed exclusively by the process view (e.g.
 * it resolved a pending kill/renice confirmation) — the caller must NOT
 * also pass that same keypress to the global handler (view-switch keys
 * '1'-'9', quit, refresh rate). Returns 0 if `ch` was not specific to a
 * pending confirmation and normal global handling should still apply.
 */
int lsm_tui_process_handle_input(lsm_tui_state_t *state, int ch, int visible_rows);

/*
 * Number of process rows visible below the two header lines (the info
 * line and the column-name line) in the process view, given the process
 * view starts at `top_row`. Shared by tui.c (which needs it to know how
 * far a PageUp/PageDown should move) and views_process.c's renderer
 * (which needs the exact same number to know how many rows it may draw)
 * — computed in exactly one place so the two can never drift out of
 * sync with each other.
 */
static inline int lsm_tui_process_visible_rows(int top_row)
{
    int rows = LINES - top_row - 2 /* header lines */ - 2 /* footer + margin */;
    return (rows < 1) ? 1 : rows;
}

#endif /* LSM_UI_TUI_INTERNAL_H */
