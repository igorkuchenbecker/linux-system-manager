#include "cpu/cpu.h"
#include "test_common.h"

static void test_read_snapshot(void)
{
    lsm_cpu_snapshot_t snap;
    lsm_status_t status = lsm_cpu_read_snapshot(&snap);
    TEST_ASSERT(status == LSM_OK, "/proc/stat should always be readable and parseable on Linux");
    TEST_ASSERT(snap.core_count >= 1, "there must be at least one core line in /proc/stat");
    TEST_ASSERT(snap.core_count <= LSM_CPU_MAX_CORES, "core_count must never exceed the array capacity");
    TEST_OK("read_snapshot");
}

static void test_read_snapshot_rejects_null(void)
{
    lsm_status_t status = lsm_cpu_read_snapshot(NULL);
    TEST_ASSERT(status == LSM_ERR_INVALID_ARG, "NULL snapshot pointer must be rejected");
    TEST_OK("read_snapshot_rejects_null");
}

static void test_usage_percent_identical_snapshots(void)
{
    lsm_cpu_times_t t = {.user = 100, .nice = 0, .system = 50, .idle = 850};
    double usage = lsm_cpu_times_usage_percent(&t, &t);
    TEST_ASSERT(usage == 0.0, "identical prev/curr snapshots must report 0%% (no elapsed ticks)");
    TEST_OK("usage_percent_identical_snapshots");
}

static void test_usage_percent_known_delta(void)
{
    /* prev: total=1000 (idle=800). curr: +100 busy, +100 idle -> total=1200,
     * idle=900. delta_total=200, delta_idle=100 -> 50% busy. */
    lsm_cpu_times_t prev = {.user = 100, .nice = 0, .system = 100, .idle = 800};
    lsm_cpu_times_t curr = {.user = 150, .nice = 0, .system = 150, .idle = 900};
    double usage = lsm_cpu_times_usage_percent(&prev, &curr);
    TEST_ASSERT(usage > 49.9 && usage < 50.1, "known jiffie delta should yield exactly 50%% usage");
    TEST_OK("usage_percent_known_delta");
}

static void test_usage_percent_full_idle(void)
{
    lsm_cpu_times_t prev = {.idle = 1000};
    lsm_cpu_times_t curr = {.idle = 2000};
    double usage = lsm_cpu_times_usage_percent(&prev, &curr);
    TEST_ASSERT(usage == 0.0, "a fully idle interval should report 0%% usage");
    TEST_OK("usage_percent_full_idle");
}

static void test_usage_percent_clock_going_backwards(void)
{
    /* Simulates counters that appear to have gone backwards (e.g. a
     * discontinuity across suspend/resume). Must not underflow into a
     * huge unsigned value or return a negative/garbage percentage. */
    lsm_cpu_times_t prev = {.user = 5000, .idle = 5000};
    lsm_cpu_times_t curr = {.user = 100, .idle = 100};
    double usage = lsm_cpu_times_usage_percent(&prev, &curr);
    TEST_ASSERT(usage == 0.0, "a backwards-moving counter pair must degrade to 0%%, not underflow");
    TEST_OK("usage_percent_clock_going_backwards");
}

static void test_load_average(void)
{
    double l1 = -1.0, l5 = -1.0, l15 = -1.0;
    lsm_status_t status = lsm_cpu_load_average(&l1, &l5, &l15);
    TEST_ASSERT(status == LSM_OK, "/proc/loadavg should always be readable on Linux");
    TEST_ASSERT(l1 >= 0.0 && l5 >= 0.0 && l15 >= 0.0, "load averages must be non-negative");
    TEST_OK("load_average");
}

static void test_load_average_rejects_null(void)
{
    double l1 = 0.0, l5 = 0.0;
    lsm_status_t status = lsm_cpu_load_average(&l1, &l5, NULL);
    TEST_ASSERT(status == LSM_ERR_INVALID_ARG, "a NULL output pointer must be rejected");
    TEST_OK("load_average_rejects_null");
}

static void test_core_frequency_degrades_gracefully(void)
{
    double mhz = -1.0;
    /* Core 0 usually exists; whether cpufreq is exposed varies by
     * platform/VM/governor, so both LSM_OK and LSM_ERR_UNSUPPORTED are
     * acceptable outcomes here — what must never happen is a crash or an
     * uninitialized `mhz`. */
    lsm_status_t status = lsm_cpu_core_frequency_mhz(0, &mhz);
    TEST_ASSERT(status == LSM_OK || status == LSM_ERR_UNSUPPORTED,
                "frequency query must either succeed or report unsupported, never fail hard");
    if (status == LSM_OK)
        TEST_ASSERT(mhz > 0.0, "a successfully read frequency must be positive");
    else
        TEST_ASSERT(mhz == 0.0, "on failure, output must be left at the documented 0.0 fallback");
    TEST_OK("core_frequency_degrades_gracefully");
}

static void test_core_frequency_rejects_null(void)
{
    lsm_status_t status = lsm_cpu_core_frequency_mhz(0, NULL);
    TEST_ASSERT(status == LSM_ERR_INVALID_ARG, "a NULL output pointer must be rejected");
    TEST_OK("core_frequency_rejects_null");
}

int main(void)
{
    test_read_snapshot();
    test_read_snapshot_rejects_null();
    test_usage_percent_identical_snapshots();
    test_usage_percent_known_delta();
    test_usage_percent_full_idle();
    test_usage_percent_clock_going_backwards();
    test_load_average();
    test_load_average_rejects_null();
    test_core_frequency_degrades_gracefully();
    test_core_frequency_rejects_null();
    return 0;
}
