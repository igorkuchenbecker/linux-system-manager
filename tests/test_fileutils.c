#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "core/status.h"
#include "test_common.h"
#include "utils/fileutils.h"

static void test_read_nonexistent(void)
{
    char buf[64];
    lsm_status_t status = lsm_read_file("/nonexistent/path/for/lsm/tests", buf, sizeof(buf), NULL);
    TEST_ASSERT(status == LSM_ERR_NOT_FOUND, "reading a missing file should return LSM_ERR_NOT_FOUND");
    TEST_OK("read_nonexistent");
}

static void test_read_exact_content(void)
{
    char path[] = "/tmp/lsm_test_fileutils_XXXXXX";
    int fd = mkstemp(path);
    TEST_ASSERT(fd >= 0, "mkstemp should succeed");
    const char *content = "hello world\n";
    ssize_t written = write(fd, content, strlen(content));
    TEST_ASSERT(written == (ssize_t)strlen(content), "write should store full content");
    close(fd);

    char buf[64];
    size_t len = 0;
    lsm_status_t status = lsm_read_file(path, buf, sizeof(buf), &len);
    TEST_ASSERT(status == LSM_OK, "reading a normal temp file should succeed");
    TEST_ASSERT(len == strlen(content), "reported length should match written content");
    TEST_ASSERT(strcmp(buf, content) == 0, "buffer content should match exactly");

    unlink(path);
    TEST_OK("read_exact_content");
}

static void test_read_truncates_to_buffer(void)
{
    char path[] = "/tmp/lsm_test_fileutils_XXXXXX";
    int fd = mkstemp(path);
    TEST_ASSERT(fd >= 0, "mkstemp should succeed");
    const char *content = "0123456789";
    ssize_t written = write(fd, content, strlen(content));
    TEST_ASSERT(written == (ssize_t)strlen(content), "write should store full content");
    close(fd);

    char buf[5]; /* capacity 4 + NUL */
    lsm_status_t status = lsm_read_file(path, buf, sizeof(buf), NULL);
    TEST_ASSERT(status == LSM_OK, "truncated read is still LSM_OK by design");
    TEST_ASSERT(strlen(buf) == 4, "buffer must never exceed bufsize - 1 bytes");
    TEST_ASSERT(strcmp(buf, "0123") == 0, "truncated content should be a clean prefix");

    unlink(path);
    TEST_OK("read_truncates_to_buffer");
}

static void test_read_proc_uptime(void)
{
    /* /proc/uptime always exists on Linux and always starts with a digit. */
    char buf[64];
    lsm_status_t status = lsm_read_file("/proc/uptime", buf, sizeof(buf), NULL);
    TEST_ASSERT(status == LSM_OK, "/proc/uptime should always be readable on Linux");
    TEST_ASSERT(buf[0] >= '0' && buf[0] <= '9', "/proc/uptime should start with a digit");
    TEST_OK("read_proc_uptime");
}

static void test_read_file_long(void)
{
    char path[] = "/tmp/lsm_test_fileutils_XXXXXX";
    int fd = mkstemp(path);
    TEST_ASSERT(fd >= 0, "mkstemp should succeed");
    const char *content = "  42\n";
    ssize_t written = write(fd, content, strlen(content));
    TEST_ASSERT(written == (ssize_t)strlen(content), "write should store full content");
    close(fd);

    long value = 0;
    lsm_status_t status = lsm_read_file_long(path, &value);
    TEST_ASSERT(status == LSM_OK, "parsing a valid integer file should succeed");
    TEST_ASSERT(value == 42, "parsed value should match file content");

    unlink(path);
    TEST_OK("read_file_long");
}

int main(void)
{
    test_read_nonexistent();
    test_read_exact_content();
    test_read_truncates_to_buffer();
    test_read_proc_uptime();
    test_read_file_long();
    return 0;
}
