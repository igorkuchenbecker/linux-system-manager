#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/log.h"
#include "core/privilege.h"
#include "core/status.h"
#include "cpu/cpu.h"
#include "disk/disk.h"
#include "gpu/gpu.h"
#include "ipc/client.h"
#include "memory/memory.h"
#include "network/network.h"
#include "sensors/sensors.h"
#include "process/process.h"
#include "system/sysinfo.h"
#include "ui/tui.h"
#include "utils/format.h"
#include "utils/timeutil.h"

#define LSM_TAG "main"

/* Sampling interval used to diff two /proc/stat snapshots into a usage%.
 * Long enough to be well above one scheduler tick (typically 4-10ms) on
 * any kernel, short enough that the CLI still feels responsive. */
#define LSM_CPU_SAMPLE_INTERVAL_NS (300L * 1000L * 1000L)

static void print_system_info(const lsm_system_info_t *info)
{
    char ram_str[32];
    char uptime_str[32];
    lsm_format_kib(info->ram_total_kib, ram_str, sizeof(ram_str));
    lsm_format_duration(info->uptime_seconds, uptime_str, sizeof(uptime_str));

    printf("Linux System Manager — System Information\n");
    printf("-------------------------------------------\n");
    printf("Hostname       : %s\n", info->hostname);
    printf("OS             : %s (%s)\n", info->os_pretty_name, info->os_id);
    printf("Kernel         : %s %s (%s)\n", info->kernel_name,
           info->kernel_release, info->architecture);
    printf("CPU model      : %s\n", info->cpu_model);
    printf("Logical CPUs   : %ld\n", info->cpu_logical_count);
    printf("RAM total      : %s\n", ram_str);
    printf("Uptime         : %s\n", uptime_str);
}

static void print_cpu_info(void)
{
    lsm_cpu_snapshot_t before, after;
    lsm_status_t status = lsm_cpu_read_snapshot(&before);
    if (status != LSM_OK) {
        LSM_LOGW(LSM_TAG, "first /proc/stat sample failed: %s", lsm_status_str(status));
        return;
    }

    struct timespec interval = {.tv_sec = 0, .tv_nsec = LSM_CPU_SAMPLE_INTERVAL_NS};
    nanosleep(&interval, NULL);

    status = lsm_cpu_read_snapshot(&after);
    if (status != LSM_OK) {
        LSM_LOGW(LSM_TAG, "second /proc/stat sample failed: %s", lsm_status_str(status));
        return;
    }

    double total_usage = lsm_cpu_times_usage_percent(&before.aggregate, &after.aggregate);

    double load1 = 0.0, load5 = 0.0, load15 = 0.0;
    lsm_status_t load_status = lsm_cpu_load_average(&load1, &load5, &load15);

    printf("\nLinux System Manager — CPU Monitor\n");
    printf("-------------------------------------------\n");
    printf("Total usage    : %.1f%%\n", total_usage);
    if (load_status == LSM_OK)
        printf("Load average   : %.2f %.2f %.2f (1/5/15 min)\n", load1, load5, load15);
    else
        printf("Load average   : N/A (%s)\n", lsm_status_str(load_status));

    printf("Per-core usage :");
    size_t core_count = before.core_count < after.core_count ? before.core_count
                                                               : after.core_count;
    for (size_t i = 0; i < core_count; i++) {
        double core_usage = lsm_cpu_times_usage_percent(&before.per_core[i],
                                                          &after.per_core[i]);
        printf(" cpu%zu=%.0f%%", i, core_usage);
    }
    printf("\n");

    double freq_mhz = 0.0;
    lsm_status_t freq_status = lsm_cpu_core_frequency_mhz(0, &freq_mhz);
    if (freq_status == LSM_OK)
        printf("cpu0 frequency : %.0f MHz\n", freq_mhz);
    else
        printf("cpu0 frequency : N/A (%s)\n", lsm_status_str(freq_status));
}

static void print_memory_info(void)
{
    lsm_memory_info_t mem;
    lsm_status_t status = lsm_memory_collect(&mem);
    if (status != LSM_OK && status != LSM_ERR_PARSE) {
        LSM_LOGW(LSM_TAG, "memory collection failed: %s", lsm_status_str(status));
        return;
    }

    char total_str[32], used_str[32], avail_str[32], buffers_str[32], cached_str[32];
    char swap_total_str[32], swap_used_str[32];
    lsm_format_kib(mem.total_kib, total_str, sizeof(total_str));
    lsm_format_kib(mem.used_kib, used_str, sizeof(used_str));
    lsm_format_kib(mem.available_kib, avail_str, sizeof(avail_str));
    lsm_format_kib(mem.buffers_kib, buffers_str, sizeof(buffers_str));
    lsm_format_kib(mem.cached_kib, cached_str, sizeof(cached_str));
    lsm_format_kib(mem.swap_total_kib, swap_total_str, sizeof(swap_total_str));
    lsm_format_kib(mem.swap_used_kib, swap_used_str, sizeof(swap_used_str));

    printf("\nLinux System Manager — Memory Monitor\n");
    printf("-------------------------------------------\n");
    printf("RAM total      : %s\n", total_str);
    printf("RAM used       : %s%s\n", used_str,
           mem.available_is_estimated ? " (estimated: no MemAvailable in /proc/meminfo)" : "");
    printf("RAM available  : %s\n", avail_str);
    printf("Buffers        : %s\n", buffers_str);
    printf("Cached         : %s\n", cached_str);
    if (mem.swap_total_kib > 0)
        printf("Swap           : %s / %s used\n", swap_used_str, swap_total_str);
    else
        printf("Swap           : none configured\n");
}

static int compare_by_rss_desc(const void *a, const void *b)
{
    const lsm_process_info_t *pa = a;
    const lsm_process_info_t *pb = b;
    if (pa->rss_kib < pb->rss_kib)
        return 1;
    if (pa->rss_kib > pb->rss_kib)
        return -1;
    return 0;
}

static const lsm_process_info_t *find_process(const lsm_process_list_t *list, pid_t pid)
{
    for (size_t i = 0; i < list->count; i++) {
        if (list->items[i].pid == pid)
            return &list->items[i];
    }
    return NULL;
}

/*
 * %CPU per process is a composition of two collectors (process + cpu),
 * deliberately performed here at the application layer rather than
 * inside process/process.c — see the rationale in process/process.h.
 * Two full process-list samples are taken 300ms apart (same interval as
 * print_cpu_info's system-wide sampling); for a handful of hundred
 * processes this is a cheap price for a CLI snapshot tool. A future
 * daemon (Phase 10) would instead keep one rolling previous sample per
 * PID and never re-scan /proc twice per refresh.
 */
static void print_process_info(void)
{
    lsm_process_list_t before = {0};
    lsm_process_list_t after = {0};
    lsm_cpu_snapshot_t cpu_before, cpu_after;

    lsm_status_t status = lsm_process_list_collect(&before);
    lsm_status_t cpu_status = lsm_cpu_read_snapshot(&cpu_before);
    if (status != LSM_OK || cpu_status != LSM_OK) {
        LSM_LOGW(LSM_TAG, "first process sample failed: %s", lsm_status_str(status));
        lsm_process_list_free(&before);
        return;
    }

    struct timespec interval = {.tv_sec = 0, .tv_nsec = LSM_CPU_SAMPLE_INTERVAL_NS};
    nanosleep(&interval, NULL);

    status = lsm_process_list_collect(&after);
    cpu_status = lsm_cpu_read_snapshot(&cpu_after);
    if (status != LSM_OK || cpu_status != LSM_OK) {
        LSM_LOGW(LSM_TAG, "second process sample failed: %s", lsm_status_str(status));
        lsm_process_list_free(&before);
        lsm_process_list_free(&after);
        return;
    }

    unsigned long long before_ticks = lsm_cpu_times_total_ticks(&cpu_before.aggregate);
    unsigned long long after_ticks = lsm_cpu_times_total_ticks(&cpu_after.aggregate);
    unsigned long long total_ticks_delta =
        (after_ticks > before_ticks) ? (after_ticks - before_ticks) : 0;

    qsort(after.items, after.count, sizeof(after.items[0]), compare_by_rss_desc);

    printf("\nLinux System Manager — Process Manager\n");
    printf("-------------------------------------------\n");
    printf("Total processes: %zu\n\n", after.count);
    printf("%-7s %-7s %-16s %s %6s %4s %10s %7s  %s\n",
           "PID", "PPID", "NAME", "S", "UID", "THR", "RSS", "CPU%", "CMD");

    size_t limit = after.count < 10 ? after.count : 10;
    for (size_t i = 0; i < limit; i++) {
        const lsm_process_info_t *p = &after.items[i];
        const lsm_process_info_t *prev = find_process(&before, p->pid);

        double cpu_percent = 0.0;
        if (prev != NULL && total_ticks_delta > 0 && p->cpu_time_ticks >= prev->cpu_time_ticks) {
            cpu_percent = (double)(p->cpu_time_ticks - prev->cpu_time_ticks) *
                          100.0 / (double)total_ticks_delta;
        }

        char rss_str[32];
        lsm_format_kib(p->rss_kib, rss_str, sizeof(rss_str));

        printf("%-7d %-7d %-16s %c %6u %4ld %10s %6.1f%%  %.60s\n",
               p->pid, p->ppid, p->name, p->state, (unsigned)p->uid,
               p->num_threads, rss_str, cpu_percent, p->cmdline);
    }

    lsm_process_list_free(&before);
    lsm_process_list_free(&after);
}

static const lsm_disk_device_t *find_device(const lsm_disk_device_list_t *list,
                                              const char *name)
{
    for (size_t i = 0; i < list->count; i++) {
        if (strcmp(list->items[i].name, name) == 0)
            return &list->items[i];
    }
    return NULL;
}

static void print_disk_info(void)
{
    lsm_mount_list_t mounts = {0};
    lsm_status_t status = lsm_disk_mounts_collect(&mounts);

    printf("\nLinux System Manager — Disk Monitor\n");
    printf("-------------------------------------------\n");

    if (status == LSM_OK) {
        printf("%-20s %-28s %-8s %10s %10s %10s\n",
               "DEVICE", "MOUNT POINT", "FSTYPE", "TOTAL", "USED", "AVAIL");
        for (size_t i = 0; i < mounts.count; i++) {
            const lsm_mount_info_t *m = &mounts.items[i];
            char total_str[32], used_str[32], avail_str[32];
            lsm_format_kib(m->total_kib, total_str, sizeof(total_str));
            lsm_format_kib(m->used_kib, used_str, sizeof(used_str));
            lsm_format_kib(m->available_kib, avail_str, sizeof(avail_str));
            printf("%-20.20s %-28.28s %-8.8s %10s %10s %10s\n",
                   m->device, m->mount_point, m->filesystem,
                   total_str, used_str, avail_str);
        }
    } else {
        LSM_LOGW(LSM_TAG, "disk mounts collection failed: %s", lsm_status_str(status));
    }
    lsm_disk_mounts_free(&mounts);

    lsm_disk_device_list_t devices_before = {0};
    lsm_disk_device_list_t devices_after = {0};
    status = lsm_disk_devices_collect(&devices_before);
    if (status != LSM_OK) {
        printf("Block devices  : N/A (%s)\n", lsm_status_str(status));
        return;
    }

    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);
    struct timespec interval = {.tv_sec = 0, .tv_nsec = LSM_CPU_SAMPLE_INTERVAL_NS};
    nanosleep(&interval, NULL);
    clock_gettime(CLOCK_MONOTONIC, &t_end);

    status = lsm_disk_devices_collect(&devices_after);
    if (status != LSM_OK) {
        LSM_LOGW(LSM_TAG, "second disk device sample failed: %s", lsm_status_str(status));
        lsm_disk_devices_free(&devices_before);
        return;
    }

    double elapsed = lsm_elapsed_seconds(&t_start, &t_end);
    if (elapsed <= 0.0)
        elapsed = 1.0; /* clock unexpectedly non-monotonic on this read; avoid div-by-zero */

    printf("\n%-16s %10s %12s %12s\n", "DEVICE", "MODEL", "READ KiB/s", "WRITE KiB/s");
    for (size_t i = 0; i < devices_after.count; i++) {
        const lsm_disk_device_t *after = &devices_after.items[i];
        const lsm_disk_device_t *before = find_device(&devices_before, after->name);
        if (before == NULL)
            continue; /* device appeared between the two samples: no rate yet */

        double read_kib_s = 0.0, write_kib_s = 0.0;
        if (after->read_sectors >= before->read_sectors)
            read_kib_s = (double)(after->read_sectors - before->read_sectors) * 512.0 /
                         1024.0 / elapsed;
        if (after->write_sectors >= before->write_sectors)
            write_kib_s = (double)(after->write_sectors - before->write_sectors) * 512.0 /
                          1024.0 / elapsed;

        printf("%-16.16s %10.10s %12.1f %12.1f\n",
               after->name, before->model, read_kib_s, write_kib_s);
    }

    lsm_disk_devices_free(&devices_before);
    lsm_disk_devices_free(&devices_after);
}

static const lsm_network_interface_t *find_interface(const lsm_network_list_t *list,
                                                        const char *name)
{
    for (size_t i = 0; i < list->count; i++) {
        if (strcmp(list->items[i].name, name) == 0)
            return &list->items[i];
    }
    return NULL;
}

static void print_network_info(void)
{
    lsm_network_list_t before = {0};
    lsm_status_t status = lsm_network_collect(&before);
    if (status != LSM_OK) {
        printf("\nLinux System Manager — Network Monitor\n");
        printf("-------------------------------------------\n");
        printf("Interfaces     : N/A (%s)\n", lsm_status_str(status));
        return;
    }

    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);
    struct timespec interval = {.tv_sec = 0, .tv_nsec = LSM_CPU_SAMPLE_INTERVAL_NS};
    nanosleep(&interval, NULL);
    clock_gettime(CLOCK_MONOTONIC, &t_end);

    lsm_network_list_t after = {0};
    status = lsm_network_collect(&after);
    if (status != LSM_OK) {
        LSM_LOGW(LSM_TAG, "second network sample failed: %s", lsm_status_str(status));
        lsm_network_free(&before);
        return;
    }

    double elapsed = lsm_elapsed_seconds(&t_start, &t_end);
    if (elapsed <= 0.0)
        elapsed = 1.0;

    printf("\nLinux System Manager — Network Monitor\n");
    printf("-------------------------------------------\n");
    printf("%-12s %-8s %12s %12s %10s %10s\n",
           "INTERFACE", "STATE", "RX KiB/s", "TX KiB/s", "RX TOTAL", "TX TOTAL");
    for (size_t i = 0; i < after.count; i++) {
        const lsm_network_interface_t *cur = &after.items[i];
        const lsm_network_interface_t *prev = find_interface(&before, cur->name);

        double rx_kib_s = 0.0, tx_kib_s = 0.0;
        if (prev != NULL && cur->rx_bytes >= prev->rx_bytes)
            rx_kib_s = (double)(cur->rx_bytes - prev->rx_bytes) / 1024.0 / elapsed;
        if (prev != NULL && cur->tx_bytes >= prev->tx_bytes)
            tx_kib_s = (double)(cur->tx_bytes - prev->tx_bytes) / 1024.0 / elapsed;

        char rx_total_str[32], tx_total_str[32];
        lsm_format_kib(cur->rx_bytes / 1024, rx_total_str, sizeof(rx_total_str));
        lsm_format_kib(cur->tx_bytes / 1024, tx_total_str, sizeof(tx_total_str));

        printf("%-12.12s %-8.8s %12.1f %12.1f %10s %10s\n",
               cur->name, cur->state, rx_kib_s, tx_kib_s, rx_total_str, tx_total_str);
    }

    lsm_network_free(&before);
    lsm_network_free(&after);
}

static void print_sensors_info(void)
{
    lsm_sensor_list_t list = {0};
    lsm_status_t status = lsm_sensors_collect(&list);

    printf("\nLinux System Manager — Hardware Sensors\n");
    printf("-------------------------------------------\n");

    if (status == LSM_ERR_UNSUPPORTED) {
        printf("Sensors        : N/A (no /sys/class/hwmon on this system)\n");
        return;
    }
    if (status != LSM_OK) {
        LSM_LOGW(LSM_TAG, "sensor collection failed: %s", lsm_status_str(status));
        return;
    }
    if (list.count == 0) {
        printf("Sensors        : none exposed by this system\n");
        lsm_sensors_free(&list);
        return;
    }

    printf("%-16s %-20s %10s\n", "CHIP", "SENSOR", "VALUE");
    for (size_t i = 0; i < list.count; i++) {
        const lsm_sensor_reading_t *r = &list.items[i];
        printf("%-16.16s %-20.20s %8.2f %s\n",
               r->chip, r->label, r->value, lsm_sensor_type_unit(r->type));
    }

    lsm_sensors_free(&list);
}

static void print_gpu_info(void)
{
    lsm_gpu_list_t list = {0};
    lsm_status_t status = lsm_gpu_collect(&list);

    printf("\nLinux System Manager — GPU\n");
    printf("-------------------------------------------\n");

    if (status != LSM_OK) {
        LSM_LOGW(LSM_TAG, "GPU collection failed: %s", lsm_status_str(status));
        return;
    }
    if (list.count == 0) {
        printf("GPU            : none detected\n");
        return;
    }

    for (size_t i = 0; i < list.count; i++) {
        const lsm_gpu_info_t *gpu = &list.items[i];
        printf("%s (driver: %s)\n", gpu->name, gpu->driver[0] != '\0' ? gpu->driver : "unknown");
        if (gpu->has_temperature)
            printf("  Temperature  : %.0f \xC2\xB0" "C\n", gpu->temperature_c);
        else
            printf("  Temperature  : N/A\n");
        if (gpu->has_utilization_percent)
            printf("  Utilization  : %.0f%%\n", gpu->utilization_percent);
        else
            printf("  Utilization  : N/A\n");
        if (gpu->has_memory) {
            char total_str[32], used_str[32];
            lsm_format_kib(gpu->memory_total_kib, total_str, sizeof(total_str));
            lsm_format_kib(gpu->memory_used_kib, used_str, sizeof(used_str));
            printf("  VRAM         : %s / %s\n", used_str, total_str);
        } else {
            printf("  VRAM         : N/A\n");
        }
    }

    lsm_gpu_free(&list);
}

static void print_snapshot(void)
{
    lsm_system_info_t info;
    lsm_status_t status = lsm_sysinfo_collect(&info);
    if (status != LSM_OK) {
        LSM_LOGW(LSM_TAG, "system info collection partially failed: %s",
                 lsm_status_str(status));
    }

    print_system_info(&info);
    print_cpu_info();
    print_memory_info();
    print_process_info();
    print_disk_info();
    print_network_info();
    print_sensors_info();
    print_gpu_info();
}

/*
 * Prints a snapshot fetched from linux-system-managerd instead of
 * collecting locally — proves the Phase 10/11 daemon+IPC round-trip
 * end to end. Process data is intentionally limited to a total count
 * here, matching the v0.1 IPC protocol scope documented in
 * ipc/protocol.h.
 */
static void print_daemon_snapshot(const lsm_ipc_snapshot_t *s)
{
    char ram_str[32], uptime_str[32];
    lsm_format_kib(s->ram_total_kib, ram_str, sizeof(ram_str));
    lsm_format_duration(s->uptime_seconds, uptime_str, sizeof(uptime_str));

    printf("Linux System Manager — Daemon Snapshot\n");
    printf("-------------------------------------------\n");
    printf("Hostname       : %s\n", s->hostname);
    printf("OS             : %s\n", s->os_pretty_name);
    printf("Kernel         : %s %s (%s)\n", s->kernel_name, s->kernel_release, s->architecture);
    printf("CPU model      : %s\n", s->cpu_model);
    printf("CPU usage      : %.1f%% (load %.2f %.2f %.2f)\n",
           s->cpu_usage_percent, s->load1, s->load5, s->load15);
    printf("RAM total      : %s\n", ram_str);

    char used_str[32], total_str2[32];
    lsm_format_kib(s->mem_used_kib, used_str, sizeof(used_str));
    lsm_format_kib(s->mem_total_kib, total_str2, sizeof(total_str2));
    printf("RAM used       : %s / %s\n", used_str, total_str2);
    printf("Uptime         : %s\n", uptime_str);
    printf("Processes      : %u\n", s->process_count);
    printf("Mounts         : %u\n", s->mount_count);
    printf("Disk devices   : %u\n", s->disk_device_count);
    printf("Net interfaces : %u\n", s->net_interface_count);
    printf("Sensors        : %u\n", s->sensor_count);
    printf("GPUs           : %u\n", s->gpu_count);
}

int main(int argc, char **argv)
{
    lsm_log_set_level(LSM_LOG_INFO);
    lsm_log_privilege_notice(LSM_TAG);

    int force_cli = 0;
    int use_daemon = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--cli") == 0)
            force_cli = 1;
        else if (strcmp(argv[i], "--daemon") == 0)
            use_daemon = 1;
    }

    if (use_daemon) {
        lsm_ipc_snapshot_t snapshot;
        lsm_status_t status = lsm_ipc_client_fetch(&snapshot);
        if (status == LSM_OK) {
            print_daemon_snapshot(&snapshot);
            return 0;
        }
        if (status == LSM_ERR_UNSUPPORTED) {
            fprintf(stderr, "linux-system-managerd is not running (or its socket is stale).\n");
            return 1;
        }
        LSM_LOGE(LSM_TAG, "failed to fetch snapshot from daemon: %s", lsm_status_str(status));
        return 1;
    }

    if (!force_cli) {
        lsm_status_t tui_status = lsm_tui_run();
        if (tui_status == LSM_OK)
            return 0;
        if (tui_status != LSM_ERR_UNSUPPORTED) {
            LSM_LOGW(LSM_TAG, "TUI failed to start (%s), falling back to snapshot mode",
                     lsm_status_str(tui_status));
        }
        /* LSM_ERR_UNSUPPORTED (stdout is not a TTY, e.g. piped/redirected
         * output) silently falls through to the one-shot snapshot below —
         * that is the expected, common non-interactive use case, not a
         * warning-worthy failure. */
    }

    print_snapshot();
    return 0;
}
