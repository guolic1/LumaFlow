#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "fluid_effect.h"
#include "imu.h"
#include "imu_platform.h"
#include "led_matrix.h"

static imu_snapshot_t sample;
static uint32_t ticks, microseconds;
static bool display_accepts;
static unsigned submissions;
static bool displayed[217];

void imu_get_snapshot(imu_snapshot_t *snapshot)
{
    *snapshot = sample;
}
uint32_t imu_platform_ticks(void)
{
    return ticks;
}
uint32_t imu_platform_time_ms(void)
{
    return ticks * 10U;
}
uint32_t imu_platform_time_us(void)
{
    microseconds += 7U;
    return microseconds;
}
bool led_matrix_submit(const bool pixels[217])
{
    if (!display_accepts)
    {
        return false;
    }
    memcpy(displayed, pixels, sizeof(displayed));
    ++submissions;
    return true;
}

static fluid_effect_stats_t stats(void)
{
    fluid_effect_stats_t result;
    fluid_effect_get_stats(&result);
    return result;
}

static void healthy(void)
{
    sample.ready = true;
    sample.valid = true;
    sample.status = IMU_OK;
    sample.timestamp_ms = imu_platform_time_ms();
    sample.sample.x = -4096;
}

static void at_time(uint32_t now_us)
{
    microseconds = now_us;
    ticks = now_us / 10000U;
    healthy();
}

static void reset(void)
{
    at_time(0U);
    display_accepts = true;
    submissions = 0U;
    fluid_effect_init();
    fluid_effect_poll();
    assert(submissions == 1U && stats().completed_steps == 0U);
}

static void finish(unsigned target)
{
    unsigned calls = 0U;
    while (stats().completed_steps < target)
    {
        fluid_effect_poll();
        assert(++calls < 100U);
    }
}

static void test_30hz_cadence(void)
{
    reset();
    /* A non-integer 100Hz tick ratio must still produce exactly 300 steps in 10s. */
    for (unsigned step = 1U; step <= 300U; ++step)
    {
        uint32_t deadline = (step * 1000000U + 29U) / 30U;
        at_time(deadline - 1U);
        fluid_effect_poll();
        assert(stats().completed_steps == step - 1U);
        at_time(deadline);
        finish(step);
        assert(stats().missed_steps == 0U);
        fluid_effect_poll();
        assert(stats().completed_steps == step); /* no free-running extra step */
    }
    fluid_effect_stats_t s = stats();
    assert(s.last_work_us > 0U && s.max_work_us >= s.last_work_us);
    assert(s.last_step_us >= s.last_work_us && s.max_slice_us == 7U);
    assert(s.state_bytes <= 3840U && submissions == 301U);
}

static void test_overrun_and_catchup(void)
{
    reset();
    at_time(33334U);
    fluid_effect_poll(); /* Begin a step, then make it cross the next deadline. */
    at_time(66667U);
    finish(1U);
    assert(stats().missed_steps == 1U);
    at_time(100000U);
    finish(2U);
    assert(stats().missed_steps == 1U);
    at_time(300000U); /* Six periods elapsed: run only the latest, skip five. */
    finish(3U);
    assert(stats().missed_steps == 6U);
    fluid_effect_poll();
    assert(stats().completed_steps == 3U);
    at_time(1400000U); /* A gap longer than one second also avoids catch-up bursts. */
    finish(4U);
    assert(stats().missed_steps == 38U);
}

static void test_faults_and_backpressure(void)
{
    reset();
    at_time(33334U);
    fluid_effect_poll();
    bool saved[217];
    memcpy(saved, displayed, sizeof(saved));
    at_time(66667U);
    sample.status = IMU_BUS_ERROR;
    for (unsigned i = 0U; i < 100U; ++i)
    {
        fluid_effect_poll();
    }
    assert(stats().completed_steps == 0U && memcmp(saved, displayed, sizeof(saved)) == 0);
    healthy();
    finish(1U);
    memcpy(saved, displayed, sizeof(saved));

    at_time(200000U);
    sample.timestamp_ms -= 101U; /* Status OK does not authorize stale data. */
    for (unsigned i = 0U; i < 100U; ++i)
    {
        fluid_effect_poll();
    }
    assert(stats().completed_steps == 1U && memcmp(saved, displayed, sizeof(saved)) == 0);
    at_time(233334U);
    finish(2U);

    reset();
    display_accepts = false;
    fluid_effect_init();
    at_time(33360U);
    for (unsigned i = 0U; i < 100U; ++i)
    {
        fluid_effect_poll();
    }
    assert(stats().completed_steps == 0U && stats().missed_steps == 1U);
    display_accepts = true;
    at_time(66700U);
    finish(1U);
    assert(stats().missed_steps == 1U);
}

static void test_clock_wraparound(void)
{
    ticks = 0xFFFFFFFEU;
    microseconds = 0xFFFFFFF0U;
    display_accepts = true;
    healthy();
    fluid_effect_init();
    ticks += 4U;
    microseconds += 33334U;
    healthy();
    finish(1U);
    fluid_effect_stats_t s = stats();
    assert(s.missed_steps == 0U && s.last_step_us < 10000U && s.max_slice_us == 7U);
}

int main(void)
{
    test_30hz_cadence();
    test_overrun_and_catchup();
    test_faults_and_backpressure();
    test_clock_wraparound();
    puts("Fluid effect scheduling tests passed");
    return 0;
}
