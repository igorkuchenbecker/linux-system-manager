#include <string.h>

#include "test_common.h"
#include "utils/strutils.h"

static void test_strlcpy_basic(void)
{
    char dst[8];
    size_t n = lsm_strlcpy(dst, "hello", sizeof(dst));
    TEST_ASSERT(n == 5, "strlcpy should return source length");
    TEST_ASSERT(strcmp(dst, "hello") == 0, "strlcpy should copy full string when it fits");
    TEST_OK("strlcpy_basic");
}

static void test_strlcpy_truncates(void)
{
    char dst[4];
    size_t n = lsm_strlcpy(dst, "hello", sizeof(dst));
    TEST_ASSERT(n == 5, "strlcpy should return the untruncated source length");
    TEST_ASSERT(strcmp(dst, "hel") == 0, "strlcpy should truncate and NUL-terminate");
    TEST_ASSERT(strlen(dst) == 3, "truncated result must fit dstsize-1");
    TEST_OK("strlcpy_truncates");
}

static void test_trim(void)
{
    char buf[] = "   hello world  \n";
    char *trimmed = lsm_str_trim(buf);
    TEST_ASSERT(strcmp(trimmed, "hello world") == 0, "trim should strip leading/trailing whitespace");
    TEST_OK("trim");
}

static void test_trim_all_whitespace(void)
{
    char buf[] = "   \t\n  ";
    char *trimmed = lsm_str_trim(buf);
    TEST_ASSERT(strcmp(trimmed, "") == 0, "trim of all-whitespace string should yield empty string");
    TEST_OK("trim_all_whitespace");
}

static void test_has_prefix(void)
{
    TEST_ASSERT(lsm_str_has_prefix("MemTotal: 123 kB", "MemTotal:"), "should detect matching prefix");
    TEST_ASSERT(!lsm_str_has_prefix("MemFree: 123 kB", "MemTotal:"), "should reject non-matching prefix");
    TEST_OK("has_prefix");
}

int main(void)
{
    test_strlcpy_basic();
    test_strlcpy_truncates();
    test_trim();
    test_trim_all_whitespace();
    test_has_prefix();
    return 0;
}
