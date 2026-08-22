#include "core/status.h"

const char *lsm_status_str(lsm_status_t status)
{
    switch (status) {
    case LSM_OK:               return "ok";
    case LSM_ERR_IO:           return "I/O error";
    case LSM_ERR_PARSE:        return "unexpected data format";
    case LSM_ERR_PERM:         return "permission denied";
    case LSM_ERR_NOT_FOUND:    return "not found";
    case LSM_ERR_UNSUPPORTED:  return "unsupported on this system";
    case LSM_ERR_NOMEM:        return "out of memory";
    case LSM_ERR_INVALID_ARG:  return "invalid argument";
    case LSM_ERR_TRANSIENT:    return "transient failure (entity vanished)";
    }
    return "unknown status";
}
