#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "fluid_effect.h"
#include "fluid_sim.h"
#include "imu.h"
#include "imu_platform.h"
#include "led_matrix.h"
#include "motion_sleep.h"
#include "motion_sleep_config.h"

static bool imu_monitoring, irq_motion, uart_running = true;

bool imu_set_motion_sleep(bool sleeping, uint16_t threshold_mg)
{
    assert(!sleeping || threshold_mg == MOTION_SLEEP_WAKE_THRESHOLD_MG);
    imu_monitoring = sleeping;
    return true;
}
bool imu_check_motion_wake(bool *motion)
{
    assert(imu_monitoring);
    *motion = irq_motion;
    irq_motion = false;
    return true;
}
void serial_uart_flush(void)
{
    assert(uart_running);
}
void serial_uart_deinit(void)
{
    assert(uart_running && imu_monitoring);
    uart_running = false;
}
void serial_uart_init(void)
{
    assert(!uart_running && !imu_monitoring);
    uart_running = true;
}

static imu_snapshot_t sample;
static uint32_t ticks, microseconds;
static bool display_accepts;
static bool display_running = true;
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
    if (!display_accepts || !display_running)
    {
        return false;
    }
    memcpy(displayed, pixels, sizeof(displayed));
    ++submissions;
    return true;
}
bool led_matrix_init(void)
{
    assert(!display_running);
    display_running = true;
    memset(displayed, 0, sizeof(displayed));
    return true;
}
void led_matrix_stop(void)
{
    assert(display_running);
    display_running = false;
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
    sample.sample.x = -(int16_t)IMU_COUNTS_PER_G;
}

static void at_time(uint32_t now_us)
{
    microseconds = now_us;
    ticks = now_us / 10000U;
    healthy();
}

static void reset(void)
{
    imu_monitoring = irq_motion = false;
    uart_running = true;
    sample = (imu_snapshot_t){0};
    at_time(0U);
    display_accepts = true;
    display_running = true;
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

static void test_8g_input_scaling(void)
{
    static fluid_sim_t reference;
    const int16_t forces[][4] = {
        {-1024, 0, -4096, 0},
        {1024, 0, 4096, 0},
        {0, -1024, 0, -4096},
        {0, 1024, 0, 4096},
        {-8192, 8191, INT16_MIN, 32764},
    };
    for (unsigned i = 0U; i < sizeof(forces) / sizeof(forces[0]); ++i)
    {
        at_time(0U);
        sample.sample.x = forces[i][0];
        sample.sample.y = forces[i][1];
        sample.sequence = 1U;
        display_accepts = display_running = true;
        fluid_effect_init();
        fluid_effect_poll();
        at_time(40000U);
        sample.sample.x = forces[i][0];
        sample.sample.y = forces[i][1];
        sample.sequence = 2U;
        finish(1U);
        fluid_sim_init(&reference);
        assert(fluid_sim_begin(&reference, forces[i][2], forces[i][3]));
        while (!fluid_sim_advance(&reference))
        {
        }
        bool expected[217];
        assert(fluid_sim_render(&reference, expected));
        /* Native +/-8g samples must have the same physical effect as the calibrated
         * 4096-units/g solver input, including the full signed sensor range. */
        assert(memcmp(displayed, expected, sizeof(expected)) == 0);
    }
}

static void test_pause_resume(void)
{
    bool expected[217], before_sleep[217];
    reset();
    sample.sample.y = 0;
    at_time(40000U);
    finish(1U);
    /* A fresh force window, without a pause, supplies the reference trajectory. */
    sample.status = IMU_BUS_ERROR;
    fluid_effect_poll();
    healthy();
    sample.sample.x = 0;
    ++sample.sequence;
    fluid_effect_poll();
    at_time(80000U);
    sample.sample.x = 0;
    ++sample.sequence;
    finish(2U);
    memcpy(expected, displayed, sizeof(expected));

    reset();
    sample.sample.y = 0;
    at_time(40000U);
    finish(1U);
    memcpy(before_sleep, displayed, sizeof(before_sleep));
    unsigned count = submissions;
    fluid_effect_set_paused(true);
    at_time(31040000U);
    sample.sample.x = 8191;
    sample.sample.y = -8192;
    ++sample.sequence;
    for (unsigned i = 0U; i < 100U; ++i)
    {
        fluid_effect_poll();
    }
    assert(stats().completed_steps == 1U && stats().missed_steps == 0U);
    assert(submissions == count && memcmp(before_sleep, displayed, sizeof(displayed)) == 0);
    fluid_effect_set_paused(true); /* idempotent */
    sample.sample.x = 0;
    sample.sample.y = 0;
    ++sample.sequence;
    fluid_effect_set_paused(false);
    fluid_effect_poll();
    assert(stats().completed_steps == 1U && stats().missed_steps == 0U);
    assert(submissions == count + 1U && memcmp(before_sleep, displayed, sizeof(displayed)) == 0);
    at_time(31080000U);
    sample.sample.x = 0;
    ++sample.sequence;
    finish(2U);
    assert(stats().missed_steps == 0U && memcmp(expected, displayed, sizeof(expected)) == 0);
}

static void test_motion_sleep_integration(void)
{
    reset();
    sample.sample = (qma6100p_sample_t){0, -(int16_t)IMU_COUNTS_PER_G, 0, 7U};
    ++sample.sequence;
    motion_sleep_init();
    assert(motion_sleep_poll());
    fluid_effect_poll();
    const unsigned sleep_tick = MOTION_SLEEP_IDLE_MS / 10U;
    for (unsigned tick = 1U; tick <= sleep_tick + 500U; ++tick)
    {
        at_time(tick * 10000U);
        sample.sample.x = 0;
        ++sample.sequence;
        for (unsigned poll = 0U; poll < 60U; ++poll)
        {
            assert(motion_sleep_poll());
            fluid_effect_poll();
        }
        assert(motion_sleep_is_sleeping() == (tick >= sleep_tick));
        assert(display_running == (tick < sleep_tick));
    }
    unsigned completed = stats().completed_steps;
    assert(imu_monitoring && !uart_running);
    assert(completed == MOTION_SLEEP_IDLE_MS / 40U - 1U && stats().missed_steps == 0U);
    unsigned count = submissions;
    bool saved[217];
    memcpy(saved, displayed, sizeof(saved));
    for (unsigned tick = sleep_tick + 501U; tick <= sleep_tick + 505U; ++tick)
    {
        at_time(tick * 10000U);
        sample.sample.x = MOTION_SLEEP_WAKE_THRESHOLD_RAW;
        if (tick == sleep_tick + 501U)
        {
            irq_motion = true;
        }
        ++sample.sequence;
        for (unsigned poll = 0U; poll < 60U; ++poll)
        {
            assert(motion_sleep_poll());
            fluid_effect_poll();
        }
        assert(!motion_sleep_is_sleeping() && display_running);
        assert(!imu_monitoring && uart_running);
        if (tick == sleep_tick + 501U)
        {
            assert(submissions == count + 1U);
            assert(memcmp(saved, displayed, sizeof(saved)) == 0);
        }
    }
    assert(stats().completed_steps == completed + 1U && stats().missed_steps == 0U);
}

static void test_pause_during_step(void)
{
    reset();
    at_time(40000U);
    fluid_effect_poll(); /* Suspend after MOVE, before the step has finished. */
    fluid_effect_set_paused(true);
    at_time(1040000U);
    fluid_effect_poll();
    assert(stats().completed_steps == 0U && stats().missed_steps == 0U);
    fluid_effect_set_paused(false);
    finish(1U);
    assert(stats().missed_steps == 0U && stats().last_step_us < 10000U);

    reset();
    display_accepts = false;
    at_time(40000U);
    finish(1U); /* Keep a completed frame pending while the display is busy. */
    unsigned count = submissions;
    fluid_effect_set_paused(true);
    at_time(1040000U);
    display_accepts = true;
    fluid_effect_set_paused(false);
    fluid_effect_poll();
    assert(stats().completed_steps == 1U && stats().missed_steps == 0U);
    assert(submissions == count + 1U);
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

static bool has_detached_droplet(void)
{
    bool visited[217] = {false};
    int16_t x[217], y[217];
    for (unsigned led = 0U; led < 217U; ++led)
    {
        assert(fluid_sim_led_position(led, &x[led], &y[led]));
    }
    bool small = false, body = false;
    for (unsigned led = 0U; led < 217U; ++led)
    {
        if (!displayed[led] || visited[led])
        {
            continue;
        }
        uint16_t queue[217];
        unsigned head = 0U, count = 1U;
        queue[0] = (uint16_t)led;
        visited[led] = true;
        while (head < count)
        {
            unsigned current = queue[head++];
            for (unsigned neighbor = 0U; neighbor < 217U; ++neighbor)
            {
                int32_t dx = x[current] - x[neighbor], dy = y[current] - y[neighbor];
                if (displayed[neighbor] && !visited[neighbor] && dx * dx + dy * dy < 715 * 715)
                {
                    visited[neighbor] = true;
                    queue[count++] = (uint16_t)neighbor;
                }
            }
        }
        small |= count <= 5U;
        body |= count >= 10U;
    }
    return small && body;
}

static void test_visible_splash(void)
{
    at_time(0U);
    sample.sample.x = 0;
    sample.sample.y = -(int16_t)IMU_COUNTS_PER_G;
    display_accepts = true;
    fluid_effect_init();
    bool detached = false;
    /* Settle, shake for one second, then let the fluid fall back down. */
    for (unsigned tick = 0U; tick <= 300U; ++tick)
    {
        at_time(tick * 10000U);
        sample.sample.x = 0;
        sample.sample.y = -(int16_t)IMU_COUNTS_PER_G;
        sample.sequence = tick + 1U;
        if (tick >= 100U && tick < 200U)
        {
            sample.sample.x = (int16_t)((int32_t)(tick * 1723U % 16384U) - 8192);
            sample.sample.y = (int16_t)((int32_t)(tick * 3527U % 16384U) - 8192);
        }
        for (unsigned poll = 0U; poll < 60U; ++poll)
        {
            fluid_effect_poll();
        }
        if (tick >= 100U && tick % 4U == 0U)
        {
            detached |= has_detached_droplet();
        }
    }
    assert(detached && stats().completed_steps == 75U && stats().missed_steps == 0U);
}

int main(void)
{
    test_8g_input_scaling();
    test_motion_sleep_integration();
    test_pause_resume();
    test_pause_during_step();
    test_visible_splash();
    test_short_shake();
    test_25hz_cadence();
    test_overrun_and_catchup();
    test_faults_and_backpressure();
    test_clock_wraparound();
    puts("Fluid effect scheduling tests passed");
    return 0;
}
