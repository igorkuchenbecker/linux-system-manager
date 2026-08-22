#include "ipc/protocol.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void lsm_ipc_socket_path(char *path, size_t path_size)
{
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (runtime_dir != NULL && runtime_dir[0] != '\0') {
        snprintf(path, path_size, "%s/linux-system-manager.sock", runtime_dir);
        return;
    }
    snprintf(path, path_size, "/tmp/linux-system-manager-%ld.sock", (long)getuid());
}

lsm_status_t lsm_ipc_write_all(int fd, const void *buf, size_t size)
{
    const char *p = buf;
    size_t remaining = size;

    while (remaining > 0) {
        ssize_t n = write(fd, p, remaining);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return LSM_ERR_IO;
        }
        if (n == 0)
            return LSM_ERR_TRANSIENT; /* peer gone */
        p += n;
        remaining -= (size_t)n;
    }
    return LSM_OK;
}

lsm_status_t lsm_ipc_read_all(int fd, void *buf, size_t size)
{
    char *p = buf;
    size_t remaining = size;

    while (remaining > 0) {
        ssize_t n = read(fd, p, remaining);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return LSM_ERR_IO;
        }
        if (n == 0)
            return LSM_ERR_TRANSIENT; /* peer closed before sending it all */
        p += n;
        remaining -= (size_t)n;
    }
    return LSM_OK;
}
