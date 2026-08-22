#ifndef LSM_DAEMON_STATE_H
#define LSM_DAEMON_STATE_H

#include <time.h>

#include "cpu/cpu.h"
#include "disk/disk.h"
#include "ipc/protocol.h"
#include "network/network.h"
#include "system/sysinfo.h"

/*
 * Internal to the daemon binary (linux-system-managerd) — not shared
 * with the TUI's lsm_tui_state_t (src/ui/tui_internal.h). The two look
 * superficially similar (both keep a rolling prev/curr sample per
 * rate-based collector) but serve genuinely different purposes: the
 * TUI's state feeds interactive rendering and holds heap-owned growable
 * lists forever until the next tick's rotation, while the daemon's job
 * is to reduce every collector's output down into one fixed-size,
 * IPC-serializable lsm_ipc_snapshot_t each tick. Sharing one struct
 * between them would couple a wire-format concern to a rendering
 * concern for no real benefit — see docs/ARCHITECTURE.md's Phase 10 note.
 */
typedef struct lsm_daemon_state {
    int sample_count;

    lsm_system_info_t sysinfo; /* collected once at startup */

    lsm_cpu_snapshot_t cpu_prev, cpu_curr;
    lsm_disk_device_list_t disk_prev, disk_curr;
    lsm_network_list_t net_prev, net_curr;

    struct timespec last_sample_time;
    double last_elapsed_seconds;

    lsm_ipc_snapshot_t cache; /* rebuilt in full on every tick */
} lsm_daemon_state_t;

void lsm_daemon_state_init(lsm_daemon_state_t *state);
void lsm_daemon_state_destroy(lsm_daemon_state_t *state);

/* Samples every collector for one tick and rebuilds `state->cache`. */
void lsm_daemon_state_tick(lsm_daemon_state_t *state);

#endif /* LSM_DAEMON_STATE_H */
