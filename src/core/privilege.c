#include "core/privilege.h"

#include <unistd.h>

#include "core/log.h"

void lsm_log_privilege_notice(const char *program_tag)
{
    uid_t ruid = getuid();
    uid_t euid = geteuid();

    if (euid == 0) {
        LSM_LOGI(program_tag,
                  "running as root (uid=%d euid=0) — not required for monitoring your own "
                  "processes; only needed to see other users' cmdlines or to signal/renice "
                  "their processes",
                  (int)ruid);
    } else {
        LSM_LOGI(program_tag, "running as uid=%d euid=%d (unprivileged, least-privilege default)",
                  (int)ruid, (int)euid);
    }
}
