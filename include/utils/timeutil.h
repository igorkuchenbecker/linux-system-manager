#ifndef LSM_UTILS_TIMEUTIL_H
#define LSM_UTILS_TIMEUTIL_H

#include <time.h>

/*
 * Seconds elapsed between two CLOCK_MONOTONIC timestamps. Used wherever
 * a rate (bytes/sec, etc.) is derived from two samples — unlike CPU tick
 * ratios (which are self-consistent regardless of real elapsed time),
 * byte-counter rates need true wall-clock time. CLOCK_MONOTONIC is
 * assumed (never wall-clock CLOCK_REALTIME, which can jump backwards).
 */
double lsm_elapsed_seconds(const struct timespec *start, const struct timespec *end);

#endif /* LSM_UTILS_TIMEUTIL_H */
