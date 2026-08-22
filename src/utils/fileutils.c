#include "utils/fileutils.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "core/log.h"

#define LSM_TAG "fileutils"

static lsm_status_t errno_to_status(int err)
{
    switch (err) {
    case ENOENT: return LSM_ERR_NOT_FOUND;
    case ESRCH:  return LSM_ERR_TRANSIENT;
    case EACCES:
    case EPERM:  return LSM_ERR_PERM;
    default:     return LSM_ERR_IO;
    }
}

lsm_status_t lsm_read_file(const char *path, char *buf, size_t bufsize,
                            size_t *out_len)
{
    if (path == NULL || buf == NULL || bufsize == 0)
        return LSM_ERR_INVALID_ARG;

    /*
     * O_CLOEXEC prevents this fd from leaking into any child process we
     * might fork() (e.g. when we later spawn helper tools); harmless here
     * but cheap and correct to set unconditionally.
     */
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        lsm_status_t status = errno_to_status(errno);
        if (status == LSM_ERR_IO)
            lsm_log_errno(LSM_TAG, path, errno);
        return status;
    }

    size_t total = 0;
    /* Leave room for the terminating NUL. */
    size_t capacity = bufsize - 1;

    while (total < capacity) {
        ssize_t n = read(fd, buf + total, capacity - total);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            lsm_status_t status = errno_to_status(errno);
            if (status == LSM_ERR_IO)
                lsm_log_errno(LSM_TAG, path, errno);
            close(fd);
            return status;
        }
        if (n == 0)
            break; /* EOF */
        total += (size_t)n;
    }

    close(fd);
    buf[total] = '\0';
    if (out_len != NULL)
        *out_len = total;
    return LSM_OK;
}

lsm_status_t lsm_read_file_long(const char *path, long *out_value)
{
    if (out_value == NULL)
        return LSM_ERR_INVALID_ARG;

    char buf[64];
    lsm_status_t status = lsm_read_file(path, buf, sizeof(buf), NULL);
    if (status != LSM_OK)
        return status;

    errno = 0;
    char *end = NULL;
    long value = strtol(buf, &end, 10);
    if (end == buf || errno == ERANGE)
        return LSM_ERR_PARSE;

    *out_value = value;
    return LSM_OK;
}
