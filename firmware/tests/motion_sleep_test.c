#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "imu.h"
#include "imu_platform.h"
#include "motion_sleep.h"
#include "motion_sleep_config.h"

static imu_snapshot_t sample;
static uint32_t now_ms;
static bool display_running, paused, restart_ok;
static bool imu_sleeping, irq_motion, wake_check_ok, arm_ok, uart_running;
static unsigned stops, starts;

bool imu_set_motion_sleep(bool value, uint16_t threshold_mg)
{
    if (value)
    {
        assert(threshold_mg == 2000U);
        if (!arm_ok)
        {
            return false;
        }
    }
    imu_sleeping = value;
    return true;
}
bool imu_check_motion_wake(bool *motion)
{
    assert(imu_sleeping);
    *motion = irq_motion;
    irq_motion = false;
    return wake_check_ok;
}
void serial_uart_flush(void)
{
    assert(uart_running);
}
void serial_uart_deinit(void)
{
    assert(uart_running && paused && !display_running);
    /* A load-switch transient must happen before the motion detector is armed. */
    irq_motion |= imu_sleeping;
    uart_running = false;
}
void serial_uart_init(void)
{
    assert(!uart_running && !imu_sleeping && display_running);
    uart_running = true;
}

void imu_get_snapshot(imu_snapshot_t *snapshot)
{
    *snapshot = sample;
}
uint32_t imu_platform_time_ms(void)
{
    return now_ms;
}
void led_matrix_stop(void)
{
    assert(display_running && paused);
    irq_motion |= imu_sleeping;
    display_running = false;
    ++stops;
}
bool led_matrix_init(void)
{
    assert(!display_running && paused);
    ++starts;
    display_running = restart_ok;
    return restart_ok;
}
void fluid_effect_set_paused(bool value)
{
    assert(value || display_running);
    paused = value;
}

static void feed(uint32_t time, int16_t x, int16_t y, int16_t z)
{
    now_ms = time;
    sample.timestamp_ms = time;
    sample.sample = (qma6100p_sample_t){x, y, z, 7U};
    ++sample.sequence;
    assert(motion_sleep_poll());
}

static void reset(uint32_t start, int16_t x, int16_t y, int16_t z)
{
    sample = (imu_snapshot_t){0};
    sample.ready = true;
    sample.valid = true;
    sample.status = IMU_OK;
    display_running = true;
    paused = false;
    restart_ok = true;
    imu_sleeping = irq_motion = false;
    wake_check_ok = arm_ok = uart_running = true;
    stops = starts = 0U;
    motion_sleep_init();
    feed(start, x, y, z);
    assert(!motion_sleep_is_sleeping());
}

static void quiet_for(uint32_t duration, int16_t x, int16_t y, int16_t z)
{
    for (uint32_t elapsed = 0U; elapsed < duration; elapsed += 10U)
    {
        feed(now_ms + 10U, x, y, z);
    }
}

static void test_static_orientations(void)
{
    const int16_t orientations[][3] = {{0, 0, IMU_COUNTS_PER_G},
                                       {IMU_COUNTS_PER_G, 0, 0},
                                       {0, -(int16_t)IMU_COUNTS_PER_G, 0},
                                       {591, 591, 591}};
    for (unsigned i = 0U; i < sizeof(orientations) / sizeof(orientations[0]); ++i)
    {
        const int16_t *a = orientations[i];
        reset(0U, a[0], a[1], a[2]);
        quiet_for(MOTION_SLEEP_IDLE_MS - 10U, a[0], a[1], a[2]);
        assert(!motion_sleep_is_sleeping() && display_running && stops == 0U);
        feed(MOTION_SLEEP_IDLE_MS, a[0], a[1], a[2]);
        assert(motion_sleep_is_sleeping() && paused && !display_running && stops == 1U);
        for (unsigned poll = 0U; poll < 10U; ++poll)
        {
            assert(motion_sleep_poll());
        }
        assert(stops == 1U && starts == 0U);
    }
}

static void test_interrupt_wake(void)
{
    reset(0U, 0, 0, IMU_COUNTS_PER_G);
    quiet_for(MOTION_SLEEP_IDLE_MS, 0, 0, IMU_COUNTS_PER_G);
    assert(imu_sleeping && !uart_running);
    sample.valid = false; /* No CPU sampling while sleeping, including a long interval. */
    now_ms += 3600000U;
    assert(motion_sleep_poll() && motion_sleep_is_sleeping());
    irq_motion = true; /* Short impulse already ended; no new raw sample is required. */
    assert(motion_sleep_poll());
    assert(!motion_sleep_is_sleeping() && uart_running && display_running && !paused &&
           starts == 1U);
    sample.valid = true;
    feed(now_ms + 10U, 0, 0, IMU_COUNTS_PER_G);
    quiet_for(MOTION_SLEEP_IDLE_MS - 10U, 0, 0, IMU_COUNTS_PER_G);
    assert(!motion_sleep_is_sleeping());
    feed(now_ms + 10U, 0, 0, IMU_COUNTS_PER_G);
    assert(motion_sleep_is_sleeping() && stops == 2U);
}

static void test_motion_and_noise(void)
{
    reset(0U, 0, 0, IMU_COUNTS_PER_G);
    for (unsigned tick = 1U; tick < 2000U; ++tick)
    {
        int16_t noise = tick % 2U ? 8 : -8;
        feed(tick * 10U, noise, -noise, (int16_t)((int32_t)IMU_COUNTS_PER_G + noise));
        assert(!motion_sleep_is_sleeping());
    }
    feed(20000U, MOTION_SLEEP_QUIET_THRESHOLD_RAW, 0, IMU_COUNTS_PER_G);
    quiet_for(MOTION_SLEEP_IDLE_MS - 10U, MOTION_SLEEP_QUIET_THRESHOLD_RAW, 0, IMU_COUNTS_PER_G);
    assert(!motion_sleep_is_sleeping());
    feed(now_ms + 10U, MOTION_SLEEP_QUIET_THRESHOLD_RAW, 0, IMU_COUNTS_PER_G);
    assert(motion_sleep_is_sleeping());
    /* Small incremental changes must accumulate rather than be discarded each sample. */
    reset(0U, 0, 0, IMU_COUNTS_PER_G);
    for (unsigned tick = 1U; tick <= 4000U; ++tick)
    {
        feed(tick * 10U, (int16_t)tick, 0, IMU_COUNTS_PER_G);
        assert(!motion_sleep_is_sleeping());
    }
}

static void test_invalid_and_missing_samples(void)
{
    reset(0U, 0, 0, IMU_COUNTS_PER_G);
    feed(MOTION_SLEEP_IDLE_MS, 0, 0,
         IMU_COUNTS_PER_G); /* Cannot infer stillness across a missing interval. */
    assert(!motion_sleep_is_sleeping());
    now_ms += MOTION_SLEEP_IDLE_MS;
    assert(motion_sleep_poll()); /* Stale duplicate must not complete the quiet timer. */
    assert(!motion_sleep_is_sleeping());
    feed(now_ms, 0, 0, IMU_COUNTS_PER_G);
    quiet_for(MOTION_SLEEP_IDLE_MS, 0, 0, IMU_COUNTS_PER_G);
    assert(motion_sleep_is_sleeping());
    wake_check_ok = false; /* A failed sensor interrupt read recovers the running mode. */
    assert(motion_sleep_poll());
    assert(!motion_sleep_is_sleeping() && !paused && display_running && uart_running);
    wake_check_ok = true;
    sample.status = IMU_OK;
    feed(now_ms + 10U, 0, 0, IMU_COUNTS_PER_G);
    quiet_for(MOTION_SLEEP_IDLE_MS - 10U, 0, 0, IMU_COUNTS_PER_G);
    assert(!motion_sleep_is_sleeping());
    sample.ready = false;
    now_ms += MOTION_SLEEP_IDLE_MS;
    assert(motion_sleep_poll());
    assert(!motion_sleep_is_sleeping());
}

static void test_sequence_wrap_and_failure(void)
{
    reset(UINT32_MAX - 10000U, 0, 0, IMU_COUNTS_PER_G);
    sample.sequence = UINT32_MAX - 5U;
    quiet_for(MOTION_SLEEP_IDLE_MS, 0, 0, IMU_COUNTS_PER_G);
    assert(motion_sleep_is_sleeping());
    sample.sample.x = MOTION_SLEEP_WAKE_THRESHOLD_RAW;
    assert(motion_sleep_poll()); /* Same sequence is not a newly acquired measurement. */
    assert(motion_sleep_is_sleeping());
    irq_motion = true;
    assert(motion_sleep_poll());
    assert(!motion_sleep_is_sleeping());

    reset(0U, 0, 0, IMU_COUNTS_PER_G);
    quiet_for(MOTION_SLEEP_IDLE_MS, 0, 0, IMU_COUNTS_PER_G);
    restart_ok = false;
    now_ms += 10U;
    sample.timestamp_ms = now_ms;
    sample.sample.x = MOTION_SLEEP_WAKE_THRESHOLD_RAW;
    ++sample.sequence;
    irq_motion = true;
    assert(!motion_sleep_poll());
    assert(motion_sleep_is_sleeping() && paused && !display_running);
}

static void test_8g_physical_thresholds(void)
{
    reset(0U, 0, 0, 1024);
    quiet_for(30000U, 511, 0, 1024); /* just below 0.5g remains quiet */
    assert(motion_sleep_is_sleeping());

    reset(0U, 0, 0, 1024); /* +1g at the new 1024 LSB/g sensitivity */
    quiet_for(20000U, 0, 0, 1024);
    feed(20000U, 512, 0, 1024); /* exactly 0.5g resets the quiet timer */
    quiet_for(10000U, 512, 0, 1024);
    assert(!motion_sleep_is_sleeping());
    quiet_for(20000U, 512, 0, 1024);
    assert(motion_sleep_is_sleeping());
    feed(now_ms + 10U, 512 + 2048, 0, 1024); /* Cached values cannot replace a hardware event. */
    assert(motion_sleep_is_sleeping());
    irq_motion = true;
    assert(motion_sleep_poll());
    assert(!motion_sleep_is_sleeping());
}

static void test_arm_failure(void)
{
    reset(0U, 0, 0, IMU_COUNTS_PER_G);
    quiet_for(MOTION_SLEEP_IDLE_MS - 10U, 0, 0, IMU_COUNTS_PER_G);
    arm_ok = false;
    now_ms += 10U;
    sample.timestamp_ms = now_ms;
    ++sample.sequence;
    assert(!motion_sleep_poll());
    /* Configuration failure is fatal to the caller; it must not enter Stop. */
    assert(!motion_sleep_is_sleeping() && !display_running && paused && !uart_running);
}

int main(void)
{
    test_8g_physical_thresholds();
    test_static_orientations();
    test_interrupt_wake();
    test_motion_and_noise();
    test_invalid_and_missing_samples();
    test_sequence_wrap_and_failure();
    test_arm_failure();
    puts("Motion sleep, wake and fault handling passed");
    return 0;
}
