#ifndef LSM_UTILS_FILEUTILS_H
#define LSM_UTILS_FILEUTILS_H

#include <stddef.h>

#include "core/status.h"

/*
 * Safe helpers for reading pseudo-files under /proc and /sys.
 *
 * These are NOT ordinary files: they have no meaningful size via stat(2)
 * (many report size 0 or 4096 regardless of actual content), can be
 * regenerated between open() and read(), can disappear between readdir()
 * and open() (TOCTOU when scanning /proc/<pid>), and may return short
 * reads. Callers must always go through these helpers rather than
 * hand-rolled fopen()/fread() so this handling lives in one place.
 */

/*
 * Reads the entire content of `path` into `buf` (capacity `bufsize`,
 * including the space for the terminating NUL). Always NUL-terminates on
 * LSM_OK. If the file is larger than `bufsize - 1`, the content is
 * truncated (this is intentional for bounded fixed-size buffers) and
 * `*out_len` reports how many bytes were actually stored.
 *
 * Maps open()/read() failures to lsm_status_t:
 *   ENOENT                -> LSM_ERR_NOT_FOUND (process/device likely gone)
 *   EACCES / EPERM        -> LSM_ERR_PERM
 *   ESRCH                 -> LSM_ERR_TRANSIENT (process vanished, /proc/<pid>)
 *   anything else         -> LSM_ERR_IO
 */
lsm_status_t lsm_read_file(const char *path, char *buf, size_t bufsize,
                            size_t *out_len);

/*
 * Reads `path` and parses it as a single integral value (base 10),
 * e.g. /sys/class/net/eth0/statistics/rx_bytes. Whitespace/newline
 * trailing the number is tolerated.
 */
lsm_status_t lsm_read_file_long(const char *path, long *out_value);

#endif /* LSM_UTILS_FILEUTILS_H */
