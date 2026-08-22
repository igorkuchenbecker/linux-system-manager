#include "state.h"

#include <string.h>
#include <time.h>

#include "core/log.h"
#include "memory/memory.h"
#include "process/process.h"
#include "sensors/sensors.h"
#include "utils/strutils.h"
#include "utils/timeutil.h"

#define LSM_TAG "daemon"

void lsm_daemon_state_init(lsm_daemon_state_t *state)
{
    memset(state, 0, sizeof(*state));
    lsm_sysinfo_collect(&state->sysinfo);
    clock_gettime(CLOCK_MONOTONIC, &state->last_sample_time);
}

void lsm_daemon_state_destroy(lsm_daemon_state_t *state)
{
    lsm_disk_devices_free(&state->disk_prev);
    lsm_disk_devices_free(&state->disk_curr);
    lsm_network_free(&state->net_prev);
    lsm_network_free(&state->net_curr);
}

static void build_static_fields(lsm_daemon_state_t *state)
{
    lsm_ipc_snapshot_t *cache = &state->cache;
    cache->protocol_version = LSM_IPC_PROTOCOL_VERSION;
    cache->struct_size = (uint32_t)sizeof(*cache);
    cache->sampled_at_unix = (int64_t)time(NULL);

    lsm_strlcpy(cache->hostname, state->sysinfo.hostname, sizeof(cache->hostname));
    lsm_strlcpy(cache->kernel_name, state->sysinfo.kernel_name, sizeof(cache->kernel_name));
    lsm_strlcpy(cache->kernel_release, state->sysinfo.kernel_release, sizeof(cache->kernel_release));
    lsm_strlcpy(cache->architecture, state->sysinfo.architecture, sizeof(cache->architecture));
    lsm_strlcpy(cache->os_pretty_name, state->sysinfo.os_pretty_name, sizeof(cache->os_pretty_name));
    lsm_strlcpy(cache->cpu_model, state->sysinfo.cpu_model, sizeof(cache->cpu_model));
    cache->cpu_logical_count = state->sysinfo.cpu_logical_count;
    cache->ram_total_kib = state->sysinfo.ram_total_kib;
    cache->uptime_seconds = state->sysinfo.uptime_seconds;
}

static void build_cpu_fields(lsm_daemon_state_t *state)
{
    lsm_ipc_snapshot_t *cache = &state->cache;

    cache->cpu_usage_percent = (state->sample_count >= 2)
        ? lsm_cpu_times_usage_percent(&state->cpu_prev.aggregate, &state->cpu_curr.aggregate)
        : 0.0;

    cache->load1 = cache->load5 = cache->load15 = 0.0;
    lsm_cpu_load_average(&cache->load1, &cache->load5, &cache->load15);

    size_t core_count = state->cpu_curr.core_count;
    if (core_count > LSM_CPU_MAX_CORES)
        core_count = LSM_CPU_MAX_CORES; /* defensive; cpu.c already enforces this cap */
    cache->cpu_core_count = (uint32_t)core_count;
    for (size_t i = 0; i < core_count; i++) {
        cache->cpu_core_usage_percent[i] = (state->sample_count >= 2)
            ? lsm_cpu_times_usage_percent(&state->cpu_prev.per_core[i], &state->cpu_curr.per_core[i])
            : 0.0;
    }
}

static void build_memory_fields(lsm_daemon_state_t *state)
{
    lsm_memory_info_t mem;
    lsm_status_t status = lsm_memory_collect(&mem);
    if (status != LSM_OK && status != LSM_ERR_PARSE)
        return; /* leave previous tick's values in cache rather than zeroing good data */

    lsm_ipc_snapshot_t *cache = &state->cache;
    cache->mem_total_kib = mem.total_kib;
    cache->mem_used_kib = mem.used_kib;
    cache->mem_available_kib = mem.available_kib;
    cache->mem_buffers_kib = mem.buffers_kib;
    cache->mem_cached_kib = mem.cached_kib;
    cache->swap_total_kib = mem.swap_total_kib;
    cache->swap_used_kib = mem.swap_used_kib;
}

static void build_process_fields(lsm_daemon_state_t *state)
{
    lsm_process_list_t list = {0};
    if (lsm_process_list_collect(&list) == LSM_OK)
        state->cache.process_count = (uint32_t)list.count;
    lsm_process_list_free(&list);
}

static void build_disk_fields(lsm_daemon_state_t *state)
{
    lsm_ipc_snapshot_t *cache = &state->cache;

    lsm_mount_list_t mounts = {0};
    if (lsm_disk_mounts_collect(&mounts) == LSM_OK) {
        uint32_t count = (uint32_t)mounts.count;
        if (count > LSM_IPC_MAX_MOUNTS) {
            LSM_LOGW(LSM_TAG, "truncating %u mounts to %d for the IPC snapshot",
                      count, LSM_IPC_MAX_MOUNTS);
            count = LSM_IPC_MAX_MOUNTS;
        }
        cache->mount_count = count;
        for (uint32_t i = 0; i < count; i++) {
            const lsm_mount_info_t *src = &mounts.items[i];
            lsm_ipc_mount_t *dst = &cache->mounts[i];
            lsm_strlcpy(dst->device, src->device, sizeof(dst->device));
            lsm_strlcpy(dst->mount_point, src->mount_point, sizeof(dst->mount_point));
            lsm_strlcpy(dst->filesystem, src->filesystem, sizeof(dst->filesystem));
            dst->total_kib = src->total_kib;
            dst->used_kib = src->used_kib;
            dst->available_kib = src->available_kib;
        }
    }
    lsm_disk_mounts_free(&mounts);

    lsm_disk_devices_free(&state->disk_prev);
    state->disk_prev = state->disk_curr;
    memset(&state->disk_curr, 0, sizeof(state->disk_curr));
    if (lsm_disk_devices_collect(&state->disk_curr) != LSM_OK)
        return;

    uint32_t count = (uint32_t)state->disk_curr.count;
    if (count > LSM_IPC_MAX_DISK_DEVICES) {
        LSM_LOGW(LSM_TAG, "truncating %u disk devices to %d for the IPC snapshot",
                  count, LSM_IPC_MAX_DISK_DEVICES);
        count = LSM_IPC_MAX_DISK_DEVICES;
    }
    cache->disk_device_count = count;

    double elapsed = state->last_elapsed_seconds;
    if (elapsed <= 0.0)
        elapsed = 1.0;

    for (uint32_t i = 0; i < count; i++) {
        const lsm_disk_device_t *cur = &state->disk_curr.items[i];
        lsm_ipc_disk_device_t *dst = &cache->disk_devices[i];
        lsm_strlcpy(dst->name, cur->name, sizeof(dst->name));
        lsm_strlcpy(dst->model, cur->model, sizeof(dst->model));
        dst->read_kib_s = 0.0;
        dst->write_kib_s = 0.0;

        if (state->sample_count < 2)
            continue;
        for (size_t j = 0; j < state->disk_prev.count; j++) {
            const lsm_disk_device_t *prev = &state->disk_prev.items[j];
            if (strcmp(prev->name, cur->name) != 0)
                continue;
            if (cur->read_sectors >= prev->read_sectors)
                dst->read_kib_s = (double)(cur->read_sectors - prev->read_sectors) * 512.0 / 1024.0 / elapsed;
            if (cur->write_sectors >= prev->write_sectors)
                dst->write_kib_s = (double)(cur->write_sectors - prev->write_sectors) * 512.0 / 1024.0 / elapsed;
            break;
        }
    }
}

static void build_network_fields(lsm_daemon_state_t *state)
{
    lsm_ipc_snapshot_t *cache = &state->cache;

    lsm_network_free(&state->net_prev);
    state->net_prev = state->net_curr;
    memset(&state->net_curr, 0, sizeof(state->net_curr));
    if (lsm_network_collect(&state->net_curr) != LSM_OK)
        return;

    uint32_t count = (uint32_t)state->net_curr.count;
    if (count > LSM_IPC_MAX_NET_INTERFACES) {
        LSM_LOGW(LSM_TAG, "truncating %u network interfaces to %d for the IPC snapshot",
                  count, LSM_IPC_MAX_NET_INTERFACES);
        count = LSM_IPC_MAX_NET_INTERFACES;
    }
    cache->net_interface_count = count;

    double elapsed = state->last_elapsed_seconds;
    if (elapsed <= 0.0)
        elapsed = 1.0;

    for (uint32_t i = 0; i < count; i++) {
        const lsm_network_interface_t *cur = &state->net_curr.items[i];
        lsm_ipc_net_interface_t *dst = &cache->net_interfaces[i];
        lsm_strlcpy(dst->name, cur->name, sizeof(dst->name));
        lsm_strlcpy(dst->state, cur->state, sizeof(dst->state));
        dst->rx_total_bytes = cur->rx_bytes;
        dst->tx_total_bytes = cur->tx_bytes;
        dst->rx_kib_s = 0.0;
        dst->tx_kib_s = 0.0;

        if (state->sample_count < 2)
            continue;
        for (size_t j = 0; j < state->net_prev.count; j++) {
            const lsm_network_interface_t *prev = &state->net_prev.items[j];
            if (strcmp(prev->name, cur->name) != 0)
                continue;
            if (cur->rx_bytes >= prev->rx_bytes)
                dst->rx_kib_s = (double)(cur->rx_bytes - prev->rx_bytes) / 1024.0 / elapsed;
            if (cur->tx_bytes >= prev->tx_bytes)
                dst->tx_kib_s = (double)(cur->tx_bytes - prev->tx_bytes) / 1024.0 / elapsed;
            break;
        }
    }
}

static void build_sensor_fields(lsm_daemon_state_t *state)
{
    lsm_sensor_list_t sensors = {0};
    if (lsm_sensors_collect(&sensors) == LSM_OK) {
        uint32_t count = (uint32_t)sensors.count;
        if (count > LSM_IPC_MAX_SENSORS) {
            LSM_LOGW(LSM_TAG, "truncating %u sensors to %d for the IPC snapshot",
                      count, LSM_IPC_MAX_SENSORS);
            count = LSM_IPC_MAX_SENSORS;
        }
        state->cache.sensor_count = count;
        for (uint32_t i = 0; i < count; i++) {
            const lsm_sensor_reading_t *src = &sensors.items[i];
            lsm_ipc_sensor_t *dst = &state->cache.sensors[i];
            lsm_strlcpy(dst->chip, src->chip, sizeof(dst->chip));
            lsm_strlcpy(dst->label, src->label, sizeof(dst->label));
            dst->type = (int32_t)src->type;
            dst->value = src->value;
        }
    }
    lsm_sensors_free(&sensors);
}

static void build_gpu_fields(lsm_daemon_state_t *state)
{
    lsm_gpu_list_t gpus = {0};
    if (lsm_gpu_collect(&gpus) == LSM_OK) {
        uint32_t count = (uint32_t)gpus.count;
        if (count > LSM_IPC_MAX_GPUS) {
            LSM_LOGW(LSM_TAG, "truncating %u GPUs to %d for the IPC snapshot",
                      count, LSM_IPC_MAX_GPUS);
            count = LSM_IPC_MAX_GPUS;
        }
        state->cache.gpu_count = count;
        for (uint32_t i = 0; i < count; i++) {
            const lsm_gpu_info_t *src = &gpus.items[i];
            lsm_ipc_gpu_t *dst = &state->cache.gpus[i];
            lsm_strlcpy(dst->name, src->name, sizeof(dst->name));
            lsm_strlcpy(dst->driver, src->driver, sizeof(dst->driver));
            dst->vendor = (int32_t)src->vendor;
            dst->has_temperature = src->has_temperature;
            dst->temperature_c = src->temperature_c;
            dst->has_utilization_percent = src->has_utilization_percent;
            dst->utilization_percent = src->utilization_percent;
            dst->has_memory = src->has_memory;
            dst->memory_total_kib = src->memory_total_kib;
            dst->memory_used_kib = src->memory_used_kib;
        }
    }
    lsm_gpu_free(&gpus);
}

void lsm_daemon_state_tick(lsm_daemon_state_t *state)
{
    lsm_cpu_snapshot_t new_cpu;
    if (lsm_cpu_read_snapshot(&new_cpu) == LSM_OK) {
        state->cpu_prev = state->cpu_curr;
        state->cpu_curr = new_cpu;
    }

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    state->last_elapsed_seconds = lsm_elapsed_seconds(&state->last_sample_time, &now);
    state->last_sample_time = now;

    build_static_fields(state);
    build_cpu_fields(state);
    build_memory_fields(state);
    build_process_fields(state);
    build_disk_fields(state);
    build_network_fields(state);
    build_sensor_fields(state);
    build_gpu_fields(state);

    if (state->sample_count < 2)
        state->sample_count++;
}
