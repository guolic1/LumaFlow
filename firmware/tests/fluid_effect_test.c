#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "fluid_effect.h"
#include "fluid_sim.h"
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

static void test_25hz_cadence(void)
{
    reset();
    /* Four 100Hz IMU ticks per step produce exactly 250 steps in 10s. */
    for (unsigned step = 1U; step <= 250U; ++step)
    {
        uint32_t deadline = step * 40000U;
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
    assert(s.state_bytes <= 3840U && submissions == 251U);
}

static void test_overrun_and_catchup(void)
{
    reset();
    at_time(40000U);
    fluid_effect_poll(); /* Begin a step, then make it cross the next deadline. */
    at_time(80000U);
    finish(1U);
    assert(stats().missed_steps == 1U);
    at_time(120000U);
    finish(2U);
    assert(stats().missed_steps == 1U);
    at_time(360000U); /* Six periods elapsed: run only the latest, skip five. */
    finish(3U);
    assert(stats().missed_steps == 6U);
    fluid_effect_poll();
    assert(stats().completed_steps == 3U);
    at_time(1480000U); /* A gap longer than one second also avoids catch-up bursts. */
    finish(4U);
    assert(stats().missed_steps == 33U);
}

static void test_faults_and_backpressure(void)
{
    reset();
    at_time(40000U);
    fluid_effect_poll();
    bool saved[217];
    memcpy(saved, displayed, sizeof(saved));
    at_time(80000U);
    sample.status = IMU_BUS_ERROR;
    for (unsigned i = 0U; i < 100U; ++i)
    {
        fluid_effect_poll();
    }
    assert(stats().completed_steps == 0U && memcmp(saved, displayed, sizeof(saved)) == 0);
    healthy();
    finish(1U);
    memcpy(saved, displayed, sizeof(saved));

    at_time(240000U);
    sample.timestamp_ms -= 101U; /* Status OK does not authorize stale data. */
    for (unsigned i = 0U; i < 100U; ++i)
    {
        fluid_effect_poll();
    }
    assert(stats().completed_steps == 1U && memcmp(saved, displayed, sizeof(saved)) == 0);
    at_time(280000U);
    finish(2U);

    reset();
    display_accepts = false;
    fluid_effect_init();
    at_time(40040U);
    for (unsigned i = 0U; i < 100U; ++i)
    {
        fluid_effect_poll();
    }
    assert(stats().completed_steps == 0U && stats().missed_steps == 1U);
    display_accepts = true;
    at_time(80040U);
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
    microseconds += 40000U;
    healthy();
    finish(1U);
    fluid_effect_stats_t s = stats();
    assert(s.missed_steps == 0U && s.last_step_us < 10000U && s.max_slice_us == 7U);
}

static void shake_frame(int16_t impulse, unsigned polls, bool invalid, bool output[217])
{
    at_time(0U);
    sample.sample.x = 0;
    sample.sample.y = 0;
    sample.sequence = 1U;
    display_accepts = true;
    fluid_effect_init();
    fluid_effect_poll();
    for (unsigned tick = 1U; tick <= 4U; ++tick)
    {
        at_time(tick * 10000U);
        sample.sample.x = tick == 1U ? impulse : 0;
        sample.sample.y = 0;
        sample.sequence = tick + 1U;
        if (invalid && tick == 1U)
        {
            sample.status = IMU_BUS_ERROR;
        }
        for (unsigned poll = 0U; poll < polls; ++poll)
        {
            fluid_effect_poll();
        }
    }
    finish(1U);
    memcpy(output, displayed, sizeof(displayed));
}

static int32_t lit_center_x(const bool pixels[217])
{
    int32_t sum = 0;
    unsigned count = 0U;
    for (unsigned led = 0U; led < 217U; ++led)
    {
        if (pixels[led])
        {
            int16_t x, y;
            assert(fluid_sim_led_position(led, &x, &y));
            sum += x;
            ++count;
        }
    }
    assert(count != 0U);
    return sum / (int32_t)count;
}

static void test_short_shake(void)
{
    bool baseline[217], left[217], right[217], repeated[217], invalid[217];
    shake_frame(0, 1U, false, baseline);
    shake_frame(8191, 1U, false, left);
    shake_frame(-8192, 1U, false, right);
    /* A 10ms pulse ending before the 40ms simulation deadline must move the LEDs. */
    assert(memcmp(left, baseline, sizeof(left)) != 0);
    assert(memcmp(right, baseline, sizeof(right)) != 0);
    assert(memcmp(left, right, sizeof(left)) != 0);
    assert(lit_center_x(left) < lit_center_x(baseline));
    assert(lit_center_x(right) > lit_center_x(baseline));
    shake_frame(-8192, 20U, false, repeated);
    assert(memcmp(right, repeated, sizeof(right)) == 0); /* Process each sample once. */
    shake_frame(-8192, 1U, true, invalid);
    assert(memcmp(baseline, invalid, sizeof(baseline)) == 0);
}

int main(void)
{
    test_short_shake();
    test_25hz_cadence();
    test_overrun_and_catchup();
    test_faults_and_backpressure();
    test_clock_wraparound();
    puts("Fluid effect scheduling tests passed");
    return 0;
}
