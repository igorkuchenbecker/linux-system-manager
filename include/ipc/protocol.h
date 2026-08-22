#ifndef LSM_IPC_PROTOCOL_H
#define LSM_IPC_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "core/status.h"
#include "cpu/cpu.h"
#include "gpu/gpu.h"
#include "sensors/sensors.h"

/*
 * Phase 10/11 — Daemon + IPC wire format.
 *
 * Shared between linux-system-managerd (the daemon, which fills and
 * writes this struct) and any client (the CLI's `--daemon` mode, and
 * eventually the TUI) that reads it. Deliberately a flat, fixed-size,
 * naturally-aligned C struct sent as raw bytes over a Unix domain
 * socket — NOT a portable network wire format:
 *
 *   - No __attribute__((packed)): both ends are the same binary,
 *     compiled by the same toolchain for the same architecture (a
 *     daemon and client on two different machines/architectures is not
 *     a supported scenario for a Unix-domain-socket-only protocol,
 *     which cannot leave the local kernel by construction), so natural
 *     struct alignment is identical on both ends and safer/faster than
 *     forcing unaligned access.
 *   - No endianness handling, for the same reason: Unix domain sockets
 *     are local-only, so there is no wire in the network sense.
 *
 * v0.1 scope: process data is NOT part of this snapshot — only the
 * total count. Every other collector (cpu/memory/disk/network/sensors/
 * gpu) is already naturally bounded (fixed arrays, or a handful of
 * mount points/interfaces/sensors on any real machine) and fits
 * cleanly into a fixed-size record; the process list is fundamentally
 * unbounded and would need a genuinely variable-length framing this
 * project has not built yet (documented in docs/ARCHITECTURE.md as the
 * natural next evolution of this protocol). Until then, a client that
 * needs the live process list calls process/process.h's
 * lsm_process_list_collect() directly, exactly as the CLI/TUI already
 * do — the daemon's cache is an *addition*, not a replacement, for
 * Phase 10/11.
 */
#define LSM_IPC_PROTOCOL_VERSION 1u

#define LSM_IPC_MAX_MOUNTS 32
#define LSM_IPC_MAX_DISK_DEVICES 16
#define LSM_IPC_MAX_NET_INTERFACES 16
#define LSM_IPC_MAX_SENSORS 64
#define LSM_IPC_MAX_GPUS 4

typedef struct lsm_ipc_mount {
    char device[160];
    char mount_point[256];
    char filesystem[32];
    uint64_t total_kib;
    uint64_t used_kib;
    uint64_t available_kib;
} lsm_ipc_mount_t;

typedef struct lsm_ipc_disk_device {
    char name[32];
    char model[160];
    double read_kib_s;
    double write_kib_s;
} lsm_ipc_disk_device_t;

typedef struct lsm_ipc_net_interface {
    char name[16];
    char state[16];
    double rx_kib_s;
    double tx_kib_s;
    uint64_t rx_total_bytes;
    uint64_t tx_total_bytes;
} lsm_ipc_net_interface_t;

typedef struct lsm_ipc_sensor {
    char chip[64];
    char label[64];
    int32_t type; /* lsm_sensor_type_t */
    double value;
} lsm_ipc_sensor_t;

typedef struct lsm_ipc_gpu {
    char name[128];
    char driver[32];
    int32_t vendor; /* lsm_gpu_vendor_t */
    int32_t has_temperature;
    double temperature_c;
    int32_t has_utilization_percent;
    double utilization_percent;
    int32_t has_memory;
    uint64_t memory_total_kib;
    uint64_t memory_used_kib;
} lsm_ipc_gpu_t;

typedef struct lsm_ipc_snapshot {
    uint32_t protocol_version; /* must equal LSM_IPC_PROTOCOL_VERSION */
    uint32_t struct_size;       /* sizeof(lsm_ipc_snapshot_t) as written by
                                  * the daemon — the minimal compatibility
                                  * check a client can do without a full
                                  * versioned field-by-field schema */
    int64_t sampled_at_unix;

    char hostname[256];
    char kernel_name[65];
    char kernel_release[65];
    char architecture[65];
    char os_pretty_name[192];
    char cpu_model[256];
    int64_t cpu_logical_count;
    uint64_t ram_total_kib;
    double uptime_seconds;

    double cpu_usage_percent;
    double load1, load5, load15;
    uint32_t cpu_core_count;
    double cpu_core_usage_percent[LSM_CPU_MAX_CORES];

    uint64_t mem_total_kib, mem_used_kib, mem_available_kib;
    uint64_t mem_buffers_kib, mem_cached_kib;
    uint64_t swap_total_kib, swap_used_kib;

    uint32_t process_count; /* see the v0.1 scope note above */

    uint32_t mount_count;
    lsm_ipc_mount_t mounts[LSM_IPC_MAX_MOUNTS];

    uint32_t disk_device_count;
    lsm_ipc_disk_device_t disk_devices[LSM_IPC_MAX_DISK_DEVICES];

    uint32_t net_interface_count;
    lsm_ipc_net_interface_t net_interfaces[LSM_IPC_MAX_NET_INTERFACES];

    uint32_t sensor_count;
    lsm_ipc_sensor_t sensors[LSM_IPC_MAX_SENSORS];

    uint32_t gpu_count;
    lsm_ipc_gpu_t gpus[LSM_IPC_MAX_GPUS];
} lsm_ipc_snapshot_t;

/*
 * Fills `path` with the daemon's Unix domain socket path: under
 * $XDG_RUNTIME_DIR (mode 0700, owned by the user — the systemd-provided
 * per-user private runtime directory) when set, falling back to
 * /tmp/linux-system-manager-<uid>.sock otherwise. The UID suffix on the
 * fallback path avoids collisions between different users' daemons on a
 * shared /tmp; XDG_RUNTIME_DIR needs no such suffix since it is already
 * private per-user.
 */
void lsm_ipc_socket_path(char *path, size_t path_size);

/*
 * Writes/reads exactly `size` bytes to/from `fd`, retrying on EINTR and
 * on short reads/writes (a Unix socket can legitimately deliver a large
 * struct across more than one read(2)/write(2) call). Returns LSM_OK,
 * LSM_ERR_TRANSIENT if the peer closed the connection before `size`
 * bytes were transferred, or LSM_ERR_IO on any other failure.
 */
lsm_status_t lsm_ipc_write_all(int fd, const void *buf, size_t size);
lsm_status_t lsm_ipc_read_all(int fd, void *buf, size_t size);

#endif /* LSM_IPC_PROTOCOL_H */
