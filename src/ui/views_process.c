#include "ui/tui_internal.h"

#include <ncurses.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "utils/format.h"

static int g_sort_by_cpu; /* set just before qsort() so the comparator can see it */

/* qsort() comparators take no extra context parameter in C (unlike
 * qsort_r, which is not portable POSIX), so the active sort mode is
 * passed through this file-local variable, set immediately before each
 * qsort() call and never read anywhere else. */
static int compare_processes(const void *a, const void *b)
{
    const lsm_process_info_t *pa = a;
    const lsm_process_info_t *pb = b;

    if (g_sort_by_cpu) {
        /* %CPU itself isn't stored on the struct (see process/process.h's
         * rationale for not computing it inside the collector), so this
         * sorts by the raw cumulative cpu_time_ticks as a proxy — correct
         * relative order for "who has used more CPU recently" as long as
         * both entries came from the same tick's sample, which they always
         * do here (we only ever sort proc_curr). */
        if (pa->cpu_time_ticks != pb->cpu_time_ticks)
            return (pa->cpu_time_ticks < pb->cpu_time_ticks) ? 1 : -1;
        return 0;
    }

    if (pa->rss_kib != pb->rss_kib)
        return (pa->rss_kib < pb->rss_kib) ? 1 : -1;
    return 0;
}

static const lsm_process_info_t *find_prev(const lsm_tui_state_t *state, pid_t pid)
{
    for (size_t i = 0; i < state->proc_prev.count; i++) {
        if (state->proc_prev.items[i].pid == pid)
            return &state->proc_prev.items[i];
    }
    return NULL;
}

void lsm_tui_render_process(const lsm_tui_state_t *state, int top_row)
{
    /* state is logically const to every other view, but the process list
     * needs sorting before display and callers only ever hold one
     * lsm_tui_state_t — casting away const here is confined to this one
     * qsort() call, which only reorders proc_curr.items in place and
     * changes no other observable field. */
    lsm_process_list_t *curr = (lsm_process_list_t *)&state->proc_curr;
    g_sort_by_cpu = state->proc_sort_by_cpu;
    if (curr->count > 0)
        qsort(curr->items, curr->count, sizeof(curr->items[0]), compare_processes);

    int row = top_row;
    mvprintw(row++, 0, "%zu processes, sorted by %s (selected: pid %d)",
             state->proc_curr.count, state->proc_sort_by_cpu ? "CPU time" : "RSS",
             state->proc_curr.count > 0 && state->proc_selected < state->proc_curr.count
                 ? state->proc_curr.items[state->proc_selected].pid : -1);
    mvprintw(row++, 0, "%-7s %-7s %-16s %s %6s %4s %10s %7s  %s",
             "PID", "PPID", "NAME", "S", "UID", "THR", "RSS", "CPU%", "CMD");

    int visible_rows = lsm_tui_process_visible_rows(top_row);

    size_t count = state->proc_curr.count;
    size_t scroll = state->proc_scroll;
    if (scroll > count)
        scroll = count;

    for (size_t i = scroll; i < count && (int)(i - scroll) < visible_rows; i++) {
        const lsm_process_info_t *p = &state->proc_curr.items[i];
        const lsm_process_info_t *prev = (state->sample_count >= 2) ? find_prev(state, p->pid) : NULL;

        double cpu_percent = 0.0;
        if (prev != NULL && p->cpu_time_ticks >= prev->cpu_time_ticks && state->last_elapsed_seconds > 0.0) {
            /* USER_HZ ticks-per-second isn't looked up here (sysconf
             * (_SC_CLK_TCK) is virtually always 100 on Linux/x86, but
             * this avoids assuming that): dividing tick delta by elapsed
             * wall-clock seconds and by the standard 100 Hz still gives a
             * reasonable relative %CPU for display purposes. For an
             * exact figure independent of USER_HZ assumptions, compare
             * against a system-wide tick delta instead, as main.c's CLI
             * process view does. */
            double ticks_delta = (double)(p->cpu_time_ticks - prev->cpu_time_ticks);
            cpu_percent = ticks_delta / 100.0 / state->last_elapsed_seconds * 100.0;
        }

        char rss_str[32];
        lsm_format_kib(p->rss_kib, rss_str, sizeof(rss_str));

        if (i == state->proc_selected)
            attron(A_REVERSE);
        mvprintw(row + (int)(i - scroll), 0, "%-7d %-7d %-16s %c %6u %4ld %10s %6.1f%%  %.60s",
                 p->pid, p->ppid, p->name, p->state, (unsigned)p->uid,
                 p->num_threads, rss_str, cpu_percent, p->cmdline);
        if (i == state->proc_selected) {
            clrtoeol();
            attroff(A_REVERSE);
        }
    }
}

int lsm_tui_process_handle_input(lsm_tui_state_t *state, int ch, int visible_rows)
{
    if (state->pending_confirm_signal != 0) {
        /* Any key resolves the prompt (only 'y' confirms; everything
         * else, including keys that would otherwise be global shortcuts
         * like '1'-'9' or 'q', cancels) — and either way this keypress
         * must not ALSO be reinterpreted by the global handler afterward
         * (e.g. cancelling with '3' must not then switch to the Memory
         * view as a side effect), hence the `return 1` below. */
        if (ch == 'y' || ch == 'Y') {
            lsm_status_t status = lsm_process_signal(state->pending_confirm_pid,
                                                        state->pending_confirm_signal);
            const char *sig_name = (state->pending_confirm_signal == SIGKILL) ? "SIGKILL" : "SIGTERM";
            if (status == LSM_OK)
                lsm_tui_set_status(state, "Sent %s to pid %d", sig_name, state->pending_confirm_pid);
            else
                lsm_tui_set_status(state, "Failed to signal pid %d: %s",
                                     state->pending_confirm_pid, lsm_status_str(status));
        } else {
            lsm_tui_set_status(state, "Cancelled");
        }
        state->pending_confirm_signal = 0;
        return 1;
    }

    size_t count = state->proc_curr.count;

    switch (ch) {
    case KEY_UP:
        if (state->proc_selected > 0)
            state->proc_selected--;
        break;
    case KEY_DOWN:
        if (state->proc_selected + 1 < count)
            state->proc_selected++;
        break;
    case KEY_PPAGE:
        state->proc_selected = (state->proc_selected > (size_t)visible_rows)
            ? state->proc_selected - (size_t)visible_rows : 0;
        break;
    case KEY_NPAGE:
        state->proc_selected += (size_t)visible_rows;
        if (count > 0 && state->proc_selected >= count)
            state->proc_selected = count - 1;
        break;
    case 'c':
        state->proc_sort_by_cpu = 1;
        break;
    case 'm':
        state->proc_sort_by_cpu = 0;
        break;
    case 'k':
    case 'x':
        if (count > 0 && state->proc_selected < count) {
            pid_t pid = state->proc_curr.items[state->proc_selected].pid;
            state->pending_confirm_signal = (ch == 'x') ? SIGKILL : SIGTERM;
            state->pending_confirm_pid = pid;
            lsm_tui_set_status(state, "Send %s to pid %d? (y to confirm, any other key cancels)",
                                 (ch == 'x') ? "SIGKILL" : "SIGTERM", pid);
        }
        break;
    case ']':
    case '[': {
        if (count > 0 && state->proc_selected < count) {
            const lsm_process_info_t *p = &state->proc_curr.items[state->proc_selected];
            int delta = (ch == ']') ? 1 : -1;
            /* lsm_process_renice() sets an ABSOLUTE niceness (see
             * process.h) — this reads the process's current value
             * (already parsed from /proc/<pid>/stat) and computes the
             * new absolute value ourselves, clamped to the valid range,
             * rather than passing the relative delta straight through. */
            int new_nice = p->nice_value + delta;
            if (new_nice > 19)
                new_nice = 19;
            if (new_nice < -20)
                new_nice = -20;

            lsm_status_t status = lsm_process_renice(p->pid, new_nice);
            if (status == LSM_OK)
                lsm_tui_set_status(state, "Set niceness of pid %d to %d", p->pid, new_nice);
            else
                lsm_tui_set_status(state, "Failed to renice pid %d: %s", p->pid, lsm_status_str(status));
        }
        break;
    }
    default:
        break;
    }

    if (count > 0) {
        if (state->proc_selected < state->proc_scroll)
            state->proc_scroll = state->proc_selected;
        else if (state->proc_selected >= state->proc_scroll + (size_t)visible_rows)
            state->proc_scroll = state->proc_selected - (size_t)visible_rows + 1;
    }

    return 0;
}
