#ifndef LSM_IPC_CLIENT_H
#define LSM_IPC_CLIENT_H

#include "core/status.h"
#include "ipc/protocol.h"

/*
 * Connects to linux-system-managerd's Unix domain socket (path from
 * lsm_ipc_socket_path()), receives one snapshot, and disconnects.
 *
 * Returns LSM_ERR_UNSUPPORTED if the daemon does not appear to be
 * running (connect() fails with ENOENT/ECONNREFUSED — no socket file,
 * or a stale one nothing is listening on) so callers can fall back to
 * direct local collection, exactly like every other "this data source
 * isn't available right now" case in this project.
 */
lsm_status_t lsm_ipc_client_fetch(lsm_ipc_snapshot_t *out);

#endif /* LSM_IPC_CLIENT_H */
