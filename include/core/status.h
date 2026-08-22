#ifndef LSM_CORE_STATUS_H
#define LSM_CORE_STATUS_H

/*
 * Unified status codes for the whole project.
 *
 * Rationale: mixing "return -1 on error" with errno inspection works but
 * forces every caller to know which failure modes map to which errno value.
 * A small closed set of status codes lets every layer (collectors, core,
 * UI) make decisions (retry? show N/A? abort?) without re-deriving meaning
 * from errno each time. The underlying errno is still preserved by callers
 * via lsm_log_errno() at the point of failure, for diagnostics.
 */
typedef enum lsm_status {
    LSM_OK = 0,
    LSM_ERR_IO,            /* read/open/stat failed unexpectedly */
    LSM_ERR_PARSE,         /* data was readable but had unexpected format */
    LSM_ERR_PERM,          /* permission denied (EACCES/EPERM) */
    LSM_ERR_NOT_FOUND,     /* file/entity does not exist (ENOENT) */
    LSM_ERR_UNSUPPORTED,   /* feature not available on this kernel/hardware */
    LSM_ERR_NOMEM,         /* allocation failure */
    LSM_ERR_INVALID_ARG,   /* programming error: bad argument to a function */
    LSM_ERR_TRANSIENT,     /* process/device vanished mid-read (TOCTOU); retry may help */
} lsm_status_t;

/* Human-readable description of a status code (static string, no alloc). */
const char *lsm_status_str(lsm_status_t status);

#endif /* LSM_CORE_STATUS_H */
