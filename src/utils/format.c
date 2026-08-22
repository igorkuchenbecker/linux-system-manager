#include "utils/format.h"

#include <stdio.h>

void lsm_format_kib(uint64_t kib, char *out, size_t outsize)
{
    static const char *units[] = {"KiB", "MiB", "GiB", "TiB", "PiB"};
    double value = (double)kib;
    size_t unit = 0;

    while (value >= 1024.0 && unit < (sizeof(units) / sizeof(units[0]) - 1)) {
        value /= 1024.0;
        unit++;
    }

    snprintf(out, outsize, "%.1f %s", value, units[unit]);
}

void lsm_format_duration(double seconds, char *out, size_t outsize)
{
    if (seconds < 0)
        seconds = 0;

    long total_seconds = (long)seconds;
    long days = total_seconds / 86400;
    long hours = (total_seconds % 86400) / 3600;
    long minutes = (total_seconds % 3600) / 60;
    long secs = total_seconds % 60;

    if (days > 0)
        snprintf(out, outsize, "%ldd %02ld:%02ld:%02ld", days, hours, minutes, secs);
    else
        snprintf(out, outsize, "%02ld:%02ld:%02ld", hours, minutes, secs);
}
