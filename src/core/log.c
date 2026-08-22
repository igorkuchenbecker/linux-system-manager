#include "core/log.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static lsm_log_level_t g_min_level = LSM_LOG_INFO;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *level_str(lsm_log_level_t level)
{
    switch (level) {
    case LSM_LOG_DEBUG: return "DEBUG";
    case LSM_LOG_INFO:  return "INFO";
    case LSM_LOG_WARN:  return "WARN";
    case LSM_LOG_ERROR: return "ERROR";
    case LSM_LOG_FATAL: return "FATAL";
    }
    return "?";
}

void lsm_log_set_level(lsm_log_level_t level)
{
    g_min_level = level;
}

void lsm_log(lsm_log_level_t level, const char *tag, const char *fmt, ...)
{
    if (level < g_min_level)
        return;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_buf;
    localtime_r(&ts.tv_sec, &tm_buf);
    char time_buf[16];
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &tm_buf);

    pthread_mutex_lock(&g_log_mutex);

    fprintf(stderr, "%s.%03ld [%-5s] %-10s ", time_buf, ts.tv_nsec / 1000000,
            level_str(level), tag ? tag : "-");

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputc('\n', stderr);

    pthread_mutex_unlock(&g_log_mutex);
}

void lsm_log_errno(const char *tag, const char *msg, int err)
{
    char errbuf[256];
    /* strerror_r (POSIX/XSI variant) avoids the shared-buffer race of strerror(). */
    if (strerror_r(err, errbuf, sizeof(errbuf)) != 0)
        snprintf(errbuf, sizeof(errbuf), "errno %d", err);
    lsm_log(LSM_LOG_ERROR, tag, "%s: %s", msg, errbuf);
}
