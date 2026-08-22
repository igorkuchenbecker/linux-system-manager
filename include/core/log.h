#ifndef LSM_CORE_LOG_H
#define LSM_CORE_LOG_H

/*
 * Minimal leveled logger.
 *
 * Writes to stderr so stdout stays clean for program output (CLI/TUI use
 * stdout for actual data; logs are diagnostics). Thread-safe: guarded by an
 * internal mutex since the daemon (Phase 10) will log from multiple
 * collector threads.
 */

typedef enum lsm_log_level {
    LSM_LOG_DEBUG = 0,
    LSM_LOG_INFO,
    LSM_LOG_WARN,
    LSM_LOG_ERROR,
    LSM_LOG_FATAL,
} lsm_log_level_t;

/* Sets the minimum level that will actually be printed. Default: LSM_LOG_INFO. */
void lsm_log_set_level(lsm_log_level_t level);

/* printf-style logging. `tag` identifies the subsystem, e.g. "cpu", "proc". */
void lsm_log(lsm_log_level_t level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/* Convenience: logs `msg: strerror(err)` at ERROR level. */
void lsm_log_errno(const char *tag, const char *msg, int err);

#define LSM_LOGD(tag, ...) lsm_log(LSM_LOG_DEBUG, tag, __VA_ARGS__)
#define LSM_LOGI(tag, ...) lsm_log(LSM_LOG_INFO,  tag, __VA_ARGS__)
#define LSM_LOGW(tag, ...) lsm_log(LSM_LOG_WARN,  tag, __VA_ARGS__)
#define LSM_LOGE(tag, ...) lsm_log(LSM_LOG_ERROR, tag, __VA_ARGS__)

#endif /* LSM_CORE_LOG_H */
