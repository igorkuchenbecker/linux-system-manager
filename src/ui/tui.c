#include "ui/tui.h"
#include "ui/tui_internal.h"

#include <locale.h>
#include <ncurses.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "utils/timeutil.h"

#define LSM_TUI_MIN_REFRESH_MS 250
#define LSM_TUI_MAX_REFRESH_MS 5000
#define LSM_TUI_STATUS_MESSAGE_SECONDS 3

static const char *view_names[LSM_VIEW_COUNT] = {
    "Dashboard", "CPU", "Memory", "Processes",
    "Disk", "Network", "Sensors", "GPU", "System",
};

void lsm_tui_set_status(lsm_tui_state_t *state, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(state->status_message, sizeof(state->status_message), fmt, args);
    va_end(args);
    state->status_message_expires_at = time(NULL) + LSM_TUI_STATUS_MESSAGE_SECONDS;
}

void lsm_tui_state_init(lsm_tui_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->view = LSM_VIEW_DASHBOARD;
    state->refresh_ms = 1000;
    state->running = 1;
    state->proc_sort_by_cpu = 1;

    lsm_sysinfo_collect(&state->sysinfo); /* degrades gracefully on its own */
    clock_gettime(CLOCK_MONOTONIC, &state->last_sample_time);
}

void lsm_tui_state_destroy(lsm_tui_state_t *state)
{
    lsm_process_list_free(&state->proc_prev);
    lsm_process_list_free(&state->proc_curr);
    lsm_disk_mounts_free(&state->mounts);
    lsm_disk_devices_free(&state->disk_prev);
    lsm_disk_devices_free(&state->disk_curr);
    lsm_network_free(&state->net_prev);
    lsm_network_free(&state->net_curr);
    lsm_sensors_free(&state->sensors);
    lsm_gpu_free(&state->gpus);
}

void lsm_tui_state_sample(lsm_tui_state_t *state)
{
    /* Rotate curr -> prev for every rate-based collector, then take a
     * fresh sample. Order matters: the old curr must become prev before
     * we overwrite curr, and the lists being rotated into *_prev must be
     * freed first (they held the *previous* prev, now two rotations
     * stale) to avoid leaking the array each tick. */
    lsm_cpu_snapshot_t old_cpu_curr = state->cpu_curr;
    state->cpu_prev = old_cpu_curr;
    lsm_cpu_read_snapshot(&state->cpu_curr);

    lsm_process_list_free(&state->proc_prev);
    state->proc_prev = state->proc_curr;
    memset(&state->proc_curr, 0, sizeof(state->proc_curr));
    lsm_process_list_collect(&state->proc_curr);

    lsm_disk_devices_free(&state->disk_prev);
    state->disk_prev = state->disk_curr;
    memset(&state->disk_curr, 0, sizeof(state->disk_curr));
    lsm_disk_devices_collect(&state->disk_curr);

    lsm_network_free(&state->net_prev);
    state->net_prev = state->net_curr;
    memset(&state->net_curr, 0, sizeof(state->net_curr));
    lsm_network_collect(&state->net_curr);

    /* Not rate-based: simply re-collected in full each tick. */
    lsm_memory_collect(&state->memory);
    lsm_disk_mounts_collect(&state->mounts);
    lsm_sensors_collect(&state->sensors);
    lsm_gpu_collect(&state->gpus);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    state->last_elapsed_seconds = lsm_elapsed_seconds(&state->last_sample_time, &now);
    state->last_sample_time = now;

    if (state->sample_count < 2)
        state->sample_count++;

    if (state->status_message[0] != '\0' && time(NULL) >= state->status_message_expires_at)
        state->status_message[0] = '\0';
}

static void render_header_footer(const lsm_tui_state_t *state)
{
    attron(A_REVERSE);
    mvhline(0, 0, ' ', COLS);
    mvprintw(0, 0, " Linux System Manager  |  %s", state->sysinfo.hostname);
    attroff(A_REVERSE);

    /* Footer: keybindings + transient status message. */
    attron(A_REVERSE);
    mvhline(LINES - 1, 0, ' ', COLS);
    if (state->status_message[0] != '\0')
        mvprintw(LINES - 1, 0, " %s", state->status_message);
    else if (state->view == LSM_VIEW_PROCESS)
        mvprintw(LINES - 1, 0,
                 " Up/Down:select  PgUp/PgDn  c:sort-cpu  m:sort-mem  k:SIGTERM  x:SIGKILL  ]/[ :nice  +/-:refresh  q:quit");
    else
        mvprintw(LINES - 1, 0,
                 " 1-9 or Tab: switch view  |  +/-: refresh rate (%dms)  |  q: quit",
                 state->refresh_ms);
    attroff(A_REVERSE);
}

/* Draws the view tab strip on row 1, e.g. "[1:Dashboard] 2:CPU 3:Memory ...". */
static void render_tabs(const lsm_tui_state_t *state)
{
    move(1, 0);
    clrtoeol();
    int x = 0;
    for (int v = 0; v < LSM_VIEW_COUNT; v++) {
        char label[32];
        snprintf(label, sizeof(label), "%d:%s", v + 1, view_names[v]);
        if ((lsm_tui_view_t)v == state->view)
            attron(A_BOLD | A_UNDERLINE);
        mvprintw(1, x, " %s ", label);
        if ((lsm_tui_view_t)v == state->view)
            attroff(A_BOLD | A_UNDERLINE);
        x += (int)strlen(label) + 3;
        if (x >= COLS)
            break;
    }
}

static void dispatch_render(const lsm_tui_state_t *state)
{
    const int top_row = 3; /* row 0: title, row 1: tabs, row 2: blank */
    switch (state->view) {
    case LSM_VIEW_DASHBOARD: lsm_tui_render_dashboard(state, top_row); break;
    case LSM_VIEW_CPU:       lsm_tui_render_cpu(state, top_row); break;
    case LSM_VIEW_MEMORY:    lsm_tui_render_memory(state, top_row); break;
    case LSM_VIEW_PROCESS:   lsm_tui_render_process(state, top_row); break;
    case LSM_VIEW_DISK:      lsm_tui_render_disk(state, top_row); break;
    case LSM_VIEW_NETWORK:   lsm_tui_render_network(state, top_row); break;
    case LSM_VIEW_SENSORS:   lsm_tui_render_sensors(state, top_row); break;
    case LSM_VIEW_GPU:       lsm_tui_render_gpu(state, top_row); break;
    case LSM_VIEW_SYSTEM:    lsm_tui_render_system(state, top_row); break;
    case LSM_VIEW_COUNT:     break;
    }
}

static void handle_global_input(lsm_tui_state_t *state, int ch)
{
    switch (ch) {
    case 'q':
    case 'Q':
        state->running = 0;
        break;
    case '\t':
        state->view = (state->view + 1) % LSM_VIEW_COUNT;
        break;
    case KEY_BTAB:
        state->view = (state->view + LSM_VIEW_COUNT - 1) % LSM_VIEW_COUNT;
        break;
    case '+':
    case '=':
        if (state->refresh_ms - 250 >= LSM_TUI_MIN_REFRESH_MS)
            state->refresh_ms -= 250;
        timeout(state->refresh_ms);
        break;
    case '-':
    case '_':
        if (state->refresh_ms + 250 <= LSM_TUI_MAX_REFRESH_MS)
            state->refresh_ms += 250;
        timeout(state->refresh_ms);
        break;
    default:
        if (ch >= '1' && ch <= '9') {
            int index = ch - '1';
            if (index < LSM_VIEW_COUNT)
                state->view = (lsm_tui_view_t)index;
        }
        break;
    }
}

lsm_status_t lsm_tui_run(void)
{
    if (!isatty(STDOUT_FILENO))
        return LSM_ERR_UNSUPPORTED;

    lsm_tui_state_t state;
    lsm_tui_state_init(&state);

    /*
     * Required before initscr() so ncursesw (the wide-character build we
     * link) treats output as UTF-8 rather than the "C" locale's
     * byte-per-cell default — without this, the U+00B0 DEGREE SIGN bytes
     * used for temperature readings render as mangled/blank cells
     * instead of "°". Deliberately LC_CTYPE only, not LC_ALL: this
     * project's numeric output (percentages, temperatures) always uses
     * '.' as the decimal point, matching the CLI snapshot mode (main.c
     * never calls setlocale() at all, so it stays in the "C" locale) —
     * LC_ALL would additionally pull in the user's LC_NUMERIC and make
     * the TUI print "52,1" instead of "52.1" on e.g. pt_BR systems,
     * inconsistently with the CLI.
     */
    setlocale(LC_CTYPE, "");

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(state.refresh_ms);
    if (has_colors()) {
        start_color();
        use_default_colors();
    }

    while (state.running) {
        lsm_tui_state_sample(&state);

        erase();
        render_header_footer(&state);
        render_tabs(&state);
        dispatch_render(&state);
        refresh();

        int ch = getch();
        if (ch != ERR) {
            int consumed = 0;
            if (state.view == LSM_VIEW_PROCESS) {
                consumed = lsm_tui_process_handle_input(&state, ch,
                                                           lsm_tui_process_visible_rows(3));
            }
            if (!consumed)
                handle_global_input(&state, ch);
        }
    }

    endwin();
    lsm_tui_state_destroy(&state);
    return LSM_OK;
}
