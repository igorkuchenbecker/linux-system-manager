#include "ipc/client.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

lsm_status_t lsm_ipc_client_fetch(lsm_ipc_snapshot_t *out)
{
    if (out == NULL)
        return LSM_ERR_INVALID_ARG;

    char socket_path[256];
    lsm_ipc_socket_path(socket_path, sizeof(socket_path));

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return LSM_ERR_IO;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(socket_path) >= sizeof(addr.sun_path)) {
        close(fd);
        return LSM_ERR_INVALID_ARG;
    }
    memcpy(addr.sun_path, socket_path, strlen(socket_path) + 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        lsm_status_t status = (errno == ENOENT || errno == ECONNREFUSED)
            ? LSM_ERR_UNSUPPORTED /* daemon not running: expected, not an error */
            : LSM_ERR_IO;
        close(fd);
        return status;
    }

    lsm_status_t status = lsm_ipc_read_all(fd, out, sizeof(*out));
    close(fd);
    if (status != LSM_OK)
        return status;

    if (out->protocol_version != LSM_IPC_PROTOCOL_VERSION ||
        out->struct_size != (uint32_t)sizeof(*out)) {
        /* A daemon built from a different (incompatible) version of this
         * project is running — refuse to trust its snapshot rather than
         * risk reading a mismatched-layout struct as if it were ours. */
        return LSM_ERR_UNSUPPORTED;
    }

    return LSM_OK;
}
