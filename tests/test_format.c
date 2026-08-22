#include <string.h>

#include "test_common.h"
#include "utils/format.h"

static void test_format_kib(void)
{
    char buf[32];

    lsm_format_kib(512, buf, sizeof(buf));
    TEST_ASSERT(strcmp(buf, "512.0 KiB") == 0, "sub-1024 KiB should stay in KiB");

    lsm_format_kib(1024 * 1024, buf, sizeof(buf));
    TEST_ASSERT(strcmp(buf, "1.0 GiB") == 0, "1024*1024 KiB should be reported as 1.0 GiB");

    lsm_format_kib(16 * 1024 * 1024, buf, sizeof(buf));
    TEST_ASSERT(strcmp(buf, "16.0 GiB") == 0, "16 GiB of RAM should format cleanly");

    TEST_OK("format_kib");
}

static void test_format_duration(void)
{
    char buf[32];

    lsm_format_duration(65.0, buf, sizeof(buf));
    TEST_ASSERT(strcmp(buf, "00:01:05") == 0, "sub-day duration should omit the day field");

    lsm_format_duration(90065.0, buf, sizeof(buf));
    TEST_ASSERT(strcmp(buf, "1d 01:01:05") == 0, "multi-day duration should include the day field");

    lsm_format_duration(-5.0, buf, sizeof(buf));
    TEST_ASSERT(strcmp(buf, "00:00:00") == 0, "negative duration should clamp to zero");

    TEST_OK("format_duration");
}

int main(void)
{
    test_format_kib();
    test_format_duration();
    return 0;
}
