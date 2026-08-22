/*
 * linux-system-managerd — background collector daemon (Phase 10/11).
 *
 * Samples every collector on a timer, keeps the result in one
 * fixed-size lsm_ipc_snapshot_t, and hands a copy of it to any client
 * that connects to a Unix domain socket. Deliberately event-driven
 * (epoll + timerfd + signalfd) rather than threaded: there is exactly
 * one writer of the cache (the timerfd-triggered sampler) and every
 * client request is answered synchronously from that same single
 * thread with data already in memory, so no lock is ever needed — see
 * docs/ARCHITECTURE.md's rationale for preferring this over pthread
 * here (the project's own rule: don't add a thread without a concrete
 * architectural need, and none exists in this design).
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/un.h>
#include <unistd.h>

#include "core/log.h"
#include "core/privilege.h"
#include "ipc/protocol.h"
#include "state.h"

#define LSM_TAG "daemond"
#define LSM_MAX_EVENTS 16
#define LSM_DEFAULT_INTERVAL_MS 1000
#define LSM_LISTEN_BACKLOG 16

static int create_signal_fd(void)
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);

    /* Block these signals from the default handler path entirely — the
     * signalfd is now the only way this process observes them, avoiding
     * the well-known hazards of doing real work inside a signal handler
     * (async-signal-safety, re-entrancy). */
    if (sigprocmask(SIG_BLOCK, &mask, NULL) != 0) {
        lsm_log_errno(LSM_TAG, "sigprocmask", errno);
        return -1;
    }

    int fd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (fd < 0)
        lsm_log_errno(LSM_TAG, "signalfd", errno);
    return fd;
}

static int create_timer_fd(int interval_ms)
{
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (fd < 0) {
        lsm_log_errno(LSM_TAG, "timerfd_create", errno);
        return -1;
    }

    struct itimerspec spec;
    memset(&spec, 0, sizeof(spec));
    spec.it_value.tv_sec = interval_ms / 1000;
    spec.it_value.tv_nsec = (long)(interval_ms % 1000) * 1000000L;
    spec.it_interval = spec.it_value; /* repeat at the same interval */

    if (timerfd_settime(fd, 0, &spec, NULL) != 0) {
        lsm_log_errno(LSM_TAG, "timerfd_settime", errno);
        close(fd);
        return -1;
    }
    return fd;
}

static int create_listen_socket(const char *path)
{
    /*
     * SOCK_NONBLOCK here is what makes the accept() loop in
     * serve_pending_connections() correctly return EAGAIN once every
     * pending connection has been drained — without it, that loop's
     * accept() would block waiting for a next connection that might
     * never come, freezing the entire single-threaded event loop
     * (including the timer-driven sampler) until a client connects.
     */
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        lsm_log_errno(LSM_TAG, "socket", errno);
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(addr.sun_path)) {
        LSM_LOGE(LSM_TAG, "socket path too long: %s", path);
        close(fd);
        return -1;
    }
    memcpy(addr.sun_path, path, strlen(path) + 1); /* length already checked above */

    /* Remove a stale socket file from a previous (crashed/killed)
     * instance. Known limitation, documented in docs/TROUBLESHOOTING.md:
     * this does not detect or prevent a second daemon instance already
     * running and listening on this same path — v0.1 has no PID-file
     * locking. Run only one daemon per user. */
    unlink(path);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        lsm_log_errno(LSM_TAG, path, errno);
        close(fd);
        return -1;
    }

    /* Defense in depth beyond the containing directory's permissions
     * (XDG_RUNTIME_DIR is already 0700, but the /tmp fallback path is
     * not private) — only the owner may connect. */
    chmod(path, S_IRUSR | S_IWUSR);

    if (listen(fd, LSM_LISTEN_BACKLOG) != 0) {
        lsm_log_errno(LSM_TAG, "listen", errno);
        close(fd);
        unlink(path);
        return -1;
    }

    return fd;
}

static void serve_pending_connections(int listen_fd, const lsm_ipc_snapshot_t *cache)
{
    for (;;) {
        /* Plain accept(), not the GNU-specific accept4(), to stay within
         * this project's chosen POSIX.1-2008 + _DEFAULT_SOURCE baseline
         * (see docs/BUILD.md) rather than pulling in _GNU_SOURCE for one
         * call — close-on-exec is set immediately after via fcntl() instead. */
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break; /* no more pending connections right now */
            if (errno == EINTR)
                continue;
            lsm_log_errno(LSM_TAG, "accept", errno);
            break;
        }
        fcntl(client_fd, F_SETFD, FD_CLOEXEC);

        /* Synchronous, single-shot response: write the whole cached
         * snapshot and close. client_fd is a normal blocking socket
         * (accept4 was not passed SOCK_NONBLOCK), so this write can
         * block briefly under memory pressure but never spin — an
         * acceptable simplicity trade-off for a payload this small
         * (tens of KB) versus building epoll-driven partial-write
         * tracking for what is currently a "connect, get one snapshot,
         * disconnect" protocol (see docs/ARCHITECTURE.md). */
        lsm_status_t status = lsm_ipc_write_all(client_fd, cache, sizeof(*cache));
        if (status != LSM_OK)
            LSM_LOGW(LSM_TAG, "failed to send snapshot to client: %s", lsm_status_str(status));

        close(client_fd);
    }
}

static volatile sig_atomic_t g_running = 1;

int main(int argc, char **argv)
{
    lsm_log_set_level(LSM_LOG_INFO);
    lsm_log_privilege_notice(LSM_TAG);

    int interval_ms = LSM_DEFAULT_INTERVAL_MS;
    for (int i = 1; i < argc; i++) {
        int parsed;
        if (sscanf(argv[i], "--interval=%d", &parsed) == 1 && parsed > 0) {
            interval_ms = parsed;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [--interval=<milliseconds>]\n", argv[0]);
            return 0;
        }
    }

    char socket_path[256];
    lsm_ipc_socket_path(socket_path, sizeof(socket_path));

    int signal_fd = create_signal_fd();
    int timer_fd = create_timer_fd(interval_ms);
    int listen_fd = create_listen_socket(socket_path);
    if (signal_fd < 0 || timer_fd < 0 || listen_fd < 0) {
        LSM_LOGE(LSM_TAG, "startup failed, exiting");
        if (signal_fd >= 0) close(signal_fd);
        if (timer_fd >= 0) close(timer_fd);
        if (listen_fd >= 0) { close(listen_fd); unlink(socket_path); }
        return 1;
    }

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        lsm_log_errno(LSM_TAG, "epoll_create1", errno);
        return 1;
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = signal_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, signal_fd, &ev);
    ev.data.fd = timer_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, timer_fd, &ev);
    ev.data.fd = listen_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev);

    lsm_daemon_state_t state;
    lsm_daemon_state_init(&state);

    LSM_LOGI(LSM_TAG, "started: interval=%dms socket=%s", interval_ms, socket_path);

    struct epoll_event events[LSM_MAX_EVENTS];
    while (g_running) {
        int n = epoll_wait(epoll_fd, events, LSM_MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            lsm_log_errno(LSM_TAG, "epoll_wait", errno);
            break;
        }

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;

            if (fd == signal_fd) {
                struct signalfd_siginfo info;
                if (read(signal_fd, &info, sizeof(info)) == sizeof(info)) {
                    LSM_LOGI(LSM_TAG, "received signal %d, shutting down", info.ssi_signo);
                }
                g_running = 0;
            } else if (fd == timer_fd) {
                uint64_t expirations = 0;
                if (read(timer_fd, &expirations, sizeof(expirations)) != sizeof(expirations)) {
                    /* EAGAIN (spurious wakeup) or a transient read error:
                     * skip this tick, the next timer firing will catch up. */
                    continue;
                }
                lsm_daemon_state_tick(&state);
            } else if (fd == listen_fd) {
                serve_pending_connections(listen_fd, &state.cache);
            }
        }
    }

    close(epoll_fd);
    close(signal_fd);
    close(timer_fd);
    close(listen_fd);
    unlink(socket_path);
    lsm_daemon_state_destroy(&state);

    LSM_LOGI(LSM_TAG, "stopped");
    return 0;
}
