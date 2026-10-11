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

static void finish(unsigned target)
{
    unsigned calls = 0U;
    while (stats().completed_steps < target)
    {
        fluid_effect_poll();
        assert(++calls < 100U);
    }
}

int main(void)
{
    ticks = 0U;
    display_accepts = true;
    healthy();
    fluid_effect_init();
    fluid_effect_poll();
    assert(submissions == 1U && stats().completed_steps == 0U);
    ticks = 1U;
    healthy();
    fluid_effect_poll();
    assert(stats().completed_steps == 0U);
    ticks = 2U;
    healthy();
    finish(1U);
    fluid_effect_stats_t s = stats();
    assert(s.last_work_us > 0U && s.max_work_us >= s.last_work_us);
    assert(s.last_step_us >= s.last_work_us && s.max_slice_us == 7U);
    assert(s.state_bytes <= 3840U && submissions == 2U && s.missed_steps == 0U);
    for (unsigned i = 0U; i < 100U; ++i)
    {
        fluid_effect_poll();
    }
    assert(stats().completed_steps == 1U); /* no free-running extra simulation */
    ticks += 6U;
    healthy();
    finish(2U);
    assert(stats().missed_steps == 2U); /* no catch-up burst */

    ticks += 2U;
    healthy();
    fluid_effect_poll(); /* pause an active step on sensor error */
    bool saved[217];
    memcpy(saved, displayed, sizeof(saved));
    sample.status = IMU_BUS_ERROR;
    ticks += 2U;
    for (unsigned i = 0U; i < 100U; ++i)
    {
        fluid_effect_poll();
    }
    assert(stats().completed_steps == 2U && memcmp(saved, displayed, sizeof(saved)) == 0);
    healthy();
    finish(3U);

    ticks += 11U; /* status OK alone does not authorize stale data */
    for (unsigned i = 0U; i < 100U; ++i)
    {
        fluid_effect_poll();
    }
    assert(stats().completed_steps == 3U);
    healthy();
    ticks += 2U;
    healthy();
    finish(4U);

    display_accepts = false;
    fluid_effect_init();
    ticks += 2U;
    healthy();
    for (unsigned i = 0U; i < 100U; ++i)
    {
        fluid_effect_poll();
    }
    assert(stats().completed_steps == 0U && stats().missed_steps == 1U);
    display_accepts = true;
    ticks += 2U;
    healthy();
    finish(1U);

    ticks = 0xFFFFFFFEU;
    microseconds = 0xFFFFFFF0U;
    healthy();
    fluid_effect_init();
    ticks += 2U;
    healthy();
    finish(1U);
    s = stats();
    assert(s.missed_steps == 0U && s.last_step_us < 10000U && s.max_slice_us == 7U);
    puts("Fluid effect scheduling tests passed");
    return 0;
}
