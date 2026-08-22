#include <errno.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#include "process/process.h"
#include "test_common.h"

static void test_read_self(void)
{
    lsm_process_info_t info;
    lsm_status_t status = lsm_process_read(getpid(), &info);
    TEST_ASSERT(status == LSM_OK, "reading our own /proc/<pid> entry must always succeed");
    TEST_ASSERT(info.pid == getpid(), "reported pid must match our own");
    TEST_ASSERT(info.uid == getuid(), "reported uid must match our own real uid");
    TEST_ASSERT(strlen(info.name) > 0, "process name must not be empty");
    TEST_ASSERT(info.num_threads >= 1, "a running process has at least one thread");
    TEST_ASSERT(info.state == 'R' || info.state == 'S' || info.state == 'D',
                "our own state while running the test must be R, S, or D");
    TEST_ASSERT(strlen(info.cmdline) > 0, "test binary was invoked with a command line");
    TEST_OK("read_self");
}

static void test_read_rejects_invalid_args(void)
{
    lsm_process_info_t info;
    TEST_ASSERT(lsm_process_read(0, &info) == LSM_ERR_INVALID_ARG, "pid 0 must be rejected");
    TEST_ASSERT(lsm_process_read(getpid(), NULL) == LSM_ERR_INVALID_ARG, "NULL output must be rejected");
    TEST_OK("read_rejects_invalid_args");
}

static void test_read_nonexistent_pid(void)
{
    lsm_process_info_t info;
    /* Far beyond any realistic pid_max, so this pid has certainly never existed. */
    lsm_status_t status = lsm_process_read(2000000000, &info);
    TEST_ASSERT(status == LSM_ERR_NOT_FOUND,
                "reading a pid that was never assigned must report LSM_ERR_NOT_FOUND");
    TEST_OK("read_nonexistent_pid");
}

static void test_list_collect_contains_self(void)
{
    lsm_process_list_t list = {0};
    lsm_status_t status = lsm_process_list_collect(&list);
    TEST_ASSERT(status == LSM_OK, "/proc enumeration should always succeed");
    TEST_ASSERT(list.count >= 1, "there must be at least one process (ourselves)");

    int found_self = 0;
    for (size_t i = 0; i < list.count; i++) {
        if (list.items[i].pid == getpid()) {
            found_self = 1;
            break;
        }
    }
    TEST_ASSERT(found_self, "our own process must appear in the enumeration");

    lsm_process_list_free(&list);
    TEST_ASSERT(list.items == NULL && list.count == 0,
                "lsm_process_list_free must reset the struct to empty");
    TEST_OK("list_collect_contains_self");
}

static void test_list_collect_rejects_null(void)
{
    lsm_status_t status = lsm_process_list_collect(NULL);
    TEST_ASSERT(status == LSM_ERR_INVALID_ARG, "NULL list pointer must be rejected");
    TEST_OK("list_collect_rejects_null");
}

static void test_signal_self_noop(void)
{
    /* Signal 0 sends nothing; it only checks existence + permission,
     * per kill(2) — always safe to use against our own process. */
    lsm_status_t status = lsm_process_signal(getpid(), 0);
    TEST_ASSERT(status == LSM_OK, "signal 0 to our own live process must succeed");
    TEST_OK("signal_self_noop");
}

static void test_signal_rejects_invalid_pid(void)
{
    lsm_status_t status = lsm_process_signal(0, 0);
    TEST_ASSERT(status == LSM_ERR_INVALID_ARG, "pid 0 must be rejected (would signal a process group)");
    TEST_OK("signal_rejects_invalid_pid");
}

static void test_signal_nonexistent_pid(void)
{
    lsm_status_t status = lsm_process_signal(2000000000, 0);
    TEST_ASSERT(status == LSM_ERR_TRANSIENT,
                "signaling a pid that does not exist must be reported, not crash");
    TEST_OK("signal_nonexistent_pid");
}

static void test_renice_self_is_idempotent(void)
{
    errno = 0;
    int current = getpriority(PRIO_PROCESS, 0);
    TEST_ASSERT(!(current == -1 && errno != 0), "getpriority on ourselves must succeed");

    lsm_status_t status = lsm_process_renice(getpid(), current);
    TEST_ASSERT(status == LSM_OK, "renicing ourselves to our current niceness must succeed");

    errno = 0;
    int after = getpriority(PRIO_PROCESS, 0);
    TEST_ASSERT(after == current, "niceness must be unchanged after renicing to the same value");
    TEST_OK("renice_self_is_idempotent");
}

static void test_renice_rejects_invalid_pid(void)
{
    lsm_status_t status = lsm_process_renice(0, 0);
    TEST_ASSERT(status == LSM_ERR_INVALID_ARG, "pid 0 must be rejected");
    TEST_OK("renice_rejects_invalid_pid");
}

int main(void)
{
    test_read_self();
    test_read_rejects_invalid_args();
    test_read_nonexistent_pid();
    test_list_collect_contains_self();
    test_list_collect_rejects_null();
    test_signal_self_noop();
    test_signal_rejects_invalid_pid();
    test_signal_nonexistent_pid();
    test_renice_self_is_idempotent();
    test_renice_rejects_invalid_pid();
    return 0;
}
