#include "ui/tui_internal.h"

#include <ncurses.h>
#include <stdio.h>
#include <string.h>

#include "utils/format.h"

/* Every render_* function stops before LINES - 2 (row LINES-1 is the
 * footer, and we leave one blank row of margin) so a small terminal
 * truncates gracefully instead of making ncurses write past the screen. */
#define LSM_TUI_BOTTOM_MARGIN 2

static int row_ok(int row)
{
    return row < LINES - LSM_TUI_BOTTOM_MARGIN;
}

void lsm_tui_render_dashboard(const lsm_tui_state_t *state, int top_row)
{
    int row = top_row;

    mvprintw(row++, 0, "%s | %s %s (%s) | %s",
             state->sysinfo.hostname, state->sysinfo.kernel_name,
             state->sysinfo.kernel_release, state->sysinfo.architecture,
             state->sysinfo.os_pretty_name);
    row++;

    double cpu_usage = (state->sample_count >= 2)
        ? lsm_cpu_times_usage_percent(&state->cpu_prev.aggregate, &state->cpu_curr.aggregate)
        : 0.0;
    double load1 = 0.0, load5 = 0.0, load15 = 0.0;
    lsm_cpu_load_average(&load1, &load5, &load15);
    if (row_ok(row))
        mvprintw(row++, 0, "CPU     %5.1f%%   load %.2f %.2f %.2f   (%ld logical CPUs)",
                 cpu_usage, load1, load5, load15, state->sysinfo.cpu_logical_count);

    char used_str[32], total_str[32];
    lsm_format_kib(state->memory.used_kib, used_str, sizeof(used_str));
    lsm_format_kib(state->memory.total_kib, total_str, sizeof(total_str));
    double mem_percent = (state->memory.total_kib > 0)
        ? (double)state->memory.used_kib * 100.0 / (double)state->memory.total_kib : 0.0;
    if (row_ok(row))
        mvprintw(row++, 0, "Memory  %5.1f%%   %s / %s", mem_percent, used_str, total_str);

    if (state->memory.swap_total_kib > 0) {
        char swap_used[32], swap_total[32];
        lsm_format_kib(state->memory.swap_used_kib, swap_used, sizeof(swap_used));
        lsm_format_kib(state->memory.swap_total_kib, swap_total, sizeof(swap_total));
        if (row_ok(row))
            mvprintw(row++, 0, "Swap             %s / %s", swap_used, swap_total);
    }

    if (row_ok(row))
        mvprintw(row++, 0, "Processes        %zu", state->proc_curr.count);

    if (state->mounts.count > 0 && row_ok(row)) {
        const lsm_mount_info_t *root = NULL;
        for (size_t i = 0; i < state->mounts.count; i++) {
            if (state->mounts.items[i].mount_point[0] == '/' &&
                state->mounts.items[i].mount_point[1] == '\0') {
                root = &state->mounts.items[i];
                break;
            }
        }
        if (root != NULL) {
            double disk_percent = (root->total_kib > 0)
                ? (double)root->used_kib * 100.0 / (double)root->total_kib : 0.0;
            char rused[32], rtotal[32];
            lsm_format_kib(root->used_kib, rused, sizeof(rused));
            lsm_format_kib(root->total_kib, rtotal, sizeof(rtotal));
            mvprintw(row++, 0, "Disk /  %5.1f%%   %s / %s", disk_percent, rused, rtotal);
        }
    }

    if (state->sensors.count > 0 && row_ok(row)) {
        double max_temp = -1000.0;
        const char *max_label = NULL;
        for (size_t i = 0; i < state->sensors.count; i++) {
            const lsm_sensor_reading_t *s = &state->sensors.items[i];
            if (s->type == LSM_SENSOR_TEMPERATURE && s->value > max_temp) {
                max_temp = s->value;
                max_label = s->label;
            }
        }
        if (max_label != NULL)
            mvprintw(row++, 0, "Hottest sensor   %s: %.1f \xC2\xB0" "C", max_label, max_temp);
    }

    if (state->gpus.count > 0 && row_ok(row)) {
        const lsm_gpu_info_t *gpu = &state->gpus.items[0];
        mvprintw(row++, 0, "GPU              %s%s", gpu->name,
                 state->gpus.count > 1 ? " (+more, see GPU tab)" : "");
    }

    row++;
    if (row_ok(row))
        mvprintw(row++, 0, "(press a number key or Tab to switch views)");
}

void lsm_tui_render_cpu(const lsm_tui_state_t *state, int top_row)
{
    int row = top_row;
    double total_usage = (state->sample_count >= 2)
        ? lsm_cpu_times_usage_percent(&state->cpu_prev.aggregate, &state->cpu_curr.aggregate)
        : 0.0;

    double load1 = 0.0, load5 = 0.0, load15 = 0.0;
    lsm_cpu_load_average(&load1, &load5, &load15);

    mvprintw(row++, 0, "Total usage : %5.1f%%", total_usage);
    mvprintw(row++, 0, "Load average: %.2f %.2f %.2f (1/5/15 min)", load1, load5, load15);
    row++;

    size_t core_count = state->cpu_curr.core_count < state->cpu_prev.core_count
        ? state->cpu_curr.core_count : state->cpu_prev.core_count;
    for (size_t i = 0; i < core_count && row_ok(row); i++) {
        double usage = (state->sample_count >= 2)
            ? lsm_cpu_times_usage_percent(&state->cpu_prev.per_core[i], &state->cpu_curr.per_core[i])
            : 0.0;

        int bar_width = 30;
        int filled = (int)(usage / 100.0 * bar_width);
        if (filled > bar_width)
            filled = bar_width;

        double freq_mhz = 0.0;
        char freq_str[32] = "N/A";
        if (lsm_cpu_core_frequency_mhz(i, &freq_mhz) == LSM_OK)
            snprintf(freq_str, sizeof(freq_str), "%.0f MHz", freq_mhz);

        mvprintw(row, 0, "cpu%-3zu [", i);
        for (int b = 0; b < bar_width; b++)
            printw("%c", b < filled ? '#' : ' ');
        printw("] %5.1f%%  %s", usage, freq_str);
        row++;
    }
}

void lsm_tui_render_memory(const lsm_tui_state_t *state, int top_row)
{
    int row = top_row;
    const lsm_memory_info_t *m = &state->memory;

    char total_str[32], used_str[32], avail_str[32], buf_str[32], cache_str[32];
    lsm_format_kib(m->total_kib, total_str, sizeof(total_str));
    lsm_format_kib(m->used_kib, used_str, sizeof(used_str));
    lsm_format_kib(m->available_kib, avail_str, sizeof(avail_str));
    lsm_format_kib(m->buffers_kib, buf_str, sizeof(buf_str));
    lsm_format_kib(m->cached_kib, cache_str, sizeof(cache_str));

    mvprintw(row++, 0, "RAM total     : %s", total_str);
    mvprintw(row++, 0, "RAM used      : %s%s", used_str,
             m->available_is_estimated ? "  (estimated)" : "");
    mvprintw(row++, 0, "RAM available : %s", avail_str);
    mvprintw(row++, 0, "Buffers       : %s", buf_str);
    mvprintw(row++, 0, "Cached        : %s", cache_str);
    row++;

    if (m->swap_total_kib > 0) {
        char su[32], st[32];
        lsm_format_kib(m->swap_used_kib, su, sizeof(su));
        lsm_format_kib(m->swap_total_kib, st, sizeof(st));
        mvprintw(row++, 0, "Swap          : %s / %s", su, st);
    } else {
        mvprintw(row++, 0, "Swap          : none configured");
    }

    row++;
    mvprintw(row++, 0, "Note: 'used' = total - MemAvailable (the kernel's own reclaimable-cache-aware");
    mvprintw(row++, 0, "estimate), not the naive total-minus-free — see docs/ARCHITECTURE.md.");
}

void lsm_tui_render_disk(const lsm_tui_state_t *state, int top_row)
{
    int row = top_row;
    mvprintw(row++, 0, "%-14s %-22s %-8s %10s %10s %10s",
             "DEVICE", "MOUNT POINT", "FSTYPE", "TOTAL", "USED", "AVAIL");
    for (size_t i = 0; i < state->mounts.count && row_ok(row); i++) {
        const lsm_mount_info_t *m = &state->mounts.items[i];
        char total_str[32], used_str[32], avail_str[32];
        lsm_format_kib(m->total_kib, total_str, sizeof(total_str));
        lsm_format_kib(m->used_kib, used_str, sizeof(used_str));
        lsm_format_kib(m->available_kib, avail_str, sizeof(avail_str));
        mvprintw(row++, 0, "%-14.14s %-22.22s %-8.8s %10s %10s %10s",
                 m->device, m->mount_point, m->filesystem, total_str, used_str, avail_str);
    }

    row++;
    if (row_ok(row))
        mvprintw(row++, 0, "%-14s %10s %12s %12s", "BLOCK DEVICE", "MODEL", "READ KiB/s", "WRITE KiB/s");

    double elapsed = state->last_elapsed_seconds;
    if (elapsed <= 0.0)
        elapsed = 1.0;

    for (size_t i = 0; i < state->disk_curr.count && row_ok(row); i++) {
        const lsm_disk_device_t *cur = &state->disk_curr.items[i];
        const lsm_disk_device_t *prev = NULL;
        for (size_t j = 0; j < state->disk_prev.count; j++) {
            if (strcmp(state->disk_prev.items[j].name, cur->name) == 0) {
                prev = &state->disk_prev.items[j];
                break;
            }
        }

        double read_kib_s = 0.0, write_kib_s = 0.0;
        if (prev != NULL && state->sample_count >= 2) {
            if (cur->read_sectors >= prev->read_sectors)
                read_kib_s = (double)(cur->read_sectors - prev->read_sectors) * 512.0 / 1024.0 / elapsed;
            if (cur->write_sectors >= prev->write_sectors)
                write_kib_s = (double)(cur->write_sectors - prev->write_sectors) * 512.0 / 1024.0 / elapsed;
        }

        mvprintw(row++, 0, "%-14.14s %10.10s %12.1f %12.1f",
                 cur->name, cur->model, read_kib_s, write_kib_s);
    }
}

void lsm_tui_render_network(const lsm_tui_state_t *state, int top_row)
{
    int row = top_row;
    mvprintw(row++, 0, "%-12s %-8s %12s %12s %10s %10s",
             "INTERFACE", "STATE", "RX KiB/s", "TX KiB/s", "RX TOTAL", "TX TOTAL");

    double elapsed = state->last_elapsed_seconds;
    if (elapsed <= 0.0)
        elapsed = 1.0;

    for (size_t i = 0; i < state->net_curr.count && row_ok(row); i++) {
        const lsm_network_interface_t *cur = &state->net_curr.items[i];
        const lsm_network_interface_t *prev = NULL;
        for (size_t j = 0; j < state->net_prev.count; j++) {
            if (strcmp(state->net_prev.items[j].name, cur->name) == 0) {
                prev = &state->net_prev.items[j];
                break;
            }
        }

        double rx_kib_s = 0.0, tx_kib_s = 0.0;
        if (prev != NULL && state->sample_count >= 2) {
            if (cur->rx_bytes >= prev->rx_bytes)
                rx_kib_s = (double)(cur->rx_bytes - prev->rx_bytes) / 1024.0 / elapsed;
            if (cur->tx_bytes >= prev->tx_bytes)
                tx_kib_s = (double)(cur->tx_bytes - prev->tx_bytes) / 1024.0 / elapsed;
        }

        char rx_total[32], tx_total[32];
        lsm_format_kib(cur->rx_bytes / 1024, rx_total, sizeof(rx_total));
        lsm_format_kib(cur->tx_bytes / 1024, tx_total, sizeof(tx_total));

        mvprintw(row++, 0, "%-12.12s %-8.8s %12.1f %12.1f %10s %10s",
                 cur->name, cur->state, rx_kib_s, tx_kib_s, rx_total, tx_total);
    }
}

void lsm_tui_render_sensors(const lsm_tui_state_t *state, int top_row)
{
    int row = top_row;
    if (state->sensors.count == 0) {
        mvprintw(row, 0, "No sensors exposed by this system (or /sys/class/hwmon absent).");
        return;
    }

    mvprintw(row++, 0, "%-16s %-20s %10s", "CHIP", "SENSOR", "VALUE");
    for (size_t i = 0; i < state->sensors.count && row_ok(row); i++) {
        const lsm_sensor_reading_t *r = &state->sensors.items[i];
        mvprintw(row++, 0, "%-16.16s %-20.20s %8.2f %s",
                 r->chip, r->label, r->value, lsm_sensor_type_unit(r->type));
    }
}

void lsm_tui_render_gpu(const lsm_tui_state_t *state, int top_row)
{
    int row = top_row;
    if (state->gpus.count == 0) {
        mvprintw(row, 0, "No GPU detected.");
        return;
    }

    for (size_t i = 0; i < state->gpus.count && row_ok(row); i++) {
        const lsm_gpu_info_t *gpu = &state->gpus.items[i];
        mvprintw(row++, 0, "%s (driver: %s)", gpu->name,
                 gpu->driver[0] != '\0' ? gpu->driver : "unknown");
        if (row_ok(row))
            mvprintw(row++, 0, "  Temperature : %s",
                     gpu->has_temperature ? "" : "N/A");
        if (gpu->has_temperature)
            mvprintw(row - 1, 16, "%.0f \xC2\xB0" "C", gpu->temperature_c);

        if (row_ok(row)) {
            if (gpu->has_utilization_percent)
                mvprintw(row++, 0, "  Utilization : %.0f%%", gpu->utilization_percent);
            else
                mvprintw(row++, 0, "  Utilization : N/A");
        }

        if (row_ok(row)) {
            if (gpu->has_memory) {
                char used_str[32], total_str[32];
                lsm_format_kib(gpu->memory_used_kib, used_str, sizeof(used_str));
                lsm_format_kib(gpu->memory_total_kib, total_str, sizeof(total_str));
                mvprintw(row++, 0, "  VRAM        : %s / %s", used_str, total_str);
            } else {
                mvprintw(row++, 0, "  VRAM        : N/A");
            }
        }
        row++;
    }
}

void lsm_tui_render_system(const lsm_tui_state_t *state, int top_row)
{
    int row = top_row;
    const lsm_system_info_t *s = &state->sysinfo;
    char ram_str[32], uptime_str[32];
    lsm_format_kib(s->ram_total_kib, ram_str, sizeof(ram_str));
    lsm_format_duration(s->uptime_seconds, uptime_str, sizeof(uptime_str));

    mvprintw(row++, 0, "Hostname     : %s", s->hostname);
    mvprintw(row++, 0, "OS           : %s (%s)", s->os_pretty_name, s->os_id);
    mvprintw(row++, 0, "Kernel       : %s %s (%s)", s->kernel_name, s->kernel_release, s->architecture);
    mvprintw(row++, 0, "CPU model    : %s", s->cpu_model);
    mvprintw(row++, 0, "Logical CPUs : %ld", s->cpu_logical_count);
    mvprintw(row++, 0, "RAM total    : %s", ram_str);
    mvprintw(row++, 0, "Uptime       : %s", uptime_str);
}
