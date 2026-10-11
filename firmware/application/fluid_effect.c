#include "fluid_effect.h"

#include <string.h>

#include "fluid_sim.h"
#include "imu.h"
#include "imu_platform.h"
#include "led_matrix.h"

typedef struct
{
    fluid_sim_t sim;
    bool pixels[LED_MATRIX_LED_COUNT];
    fluid_effect_stats_t stats;
    uint32_t handled_ticks;
    uint32_t step_started_us;
    uint32_t work_us;
    int32_t filtered_x;
    int32_t filtered_y;
    bool pending;
} fluid_effect_t;

static fluid_effect_t effect;

_Static_assert(sizeof(fluid_effect_t) <= 3840U, "Fluid storage exceeds its RAM budget");
_Static_assert(FLUID_EFFECT_RATE_HZ == FLUID_SIM_RATE_HZ,
               "Fluid scheduling and integration must use the same rate");
_Static_assert(IMU_SAMPLE_RATE_HZ % FLUID_EFFECT_RATE_HZ == 0U,
               "Fluid scheduling requires an integer number of IMU ticks per step");

#define IMU_TICKS_PER_STEP (IMU_SAMPLE_RATE_HZ / FLUID_EFFECT_RATE_HZ)

void fluid_effect_init(void)
{
    memset(&effect, 0, sizeof(effect));
    fluid_sim_init(&effect.sim);
    effect.pending = fluid_sim_render(&effect.sim, effect.pixels);
    effect.handled_ticks = imu_platform_ticks();
    effect.stats.state_bytes = (uint16_t)sizeof(effect);
}

void fluid_effect_poll(void)
{
    imu_snapshot_t snapshot;
    imu_get_snapshot(&snapshot);
    bool healthy = snapshot.ready && snapshot.valid && snapshot.status == IMU_OK &&
                   (uint32_t)(imu_platform_time_ms() - snapshot.timestamp_ms) <= 100U;
    if (effect.pending && led_matrix_submit(effect.pixels))
    {
        effect.pending = false;
    }
    uint32_t ticks = imu_platform_ticks();
    /* Four IMU periods make one 40ms step; preserve partial periods across polls. */
    uint32_t periods = (uint32_t)(ticks - effect.handled_ticks) / IMU_TICKS_PER_STEP;
    if (periods != 0U)
    {
        effect.handled_ticks += periods * IMU_TICKS_PER_STEP;
        effect.stats.missed_steps += periods - 1U;
        if (healthy && !effect.pending && effect.sim.phase == FLUID_PHASE_IDLE)
        {
            effect.filtered_x += ((int32_t)snapshot.sample.x - effect.filtered_x) / 4;
            effect.filtered_y += ((int32_t)snapshot.sample.y - effect.filtered_y) / 4;
            (void)fluid_sim_begin(&effect.sim, (int16_t)effect.filtered_x,
                                  (int16_t)effect.filtered_y);
            effect.step_started_us = imu_platform_time_us();
            effect.work_us = 0U;
        }
        else
        {
            ++effect.stats.missed_steps;
        }
    }
    if (!healthy || effect.pending || effect.sim.phase == FLUID_PHASE_IDLE)
    {
        return;
    }
    uint32_t started_us = imu_platform_time_us();
    bool completed = fluid_sim_advance(&effect.sim);
    if (completed)
    {
        effect.pending = fluid_sim_render(&effect.sim, effect.pixels);
    }
    uint32_t finished_us = imu_platform_time_us();
    uint32_t duration_us = (uint32_t)(finished_us - started_us);
    effect.work_us += duration_us;
    if (duration_us > effect.stats.max_slice_us)
    {
        effect.stats.max_slice_us = duration_us;
    }
    if (completed)
    {
        ++effect.stats.completed_steps;
        effect.stats.last_work_us = effect.work_us;
        if (effect.work_us > effect.stats.max_work_us)
        {
            effect.stats.max_work_us = effect.work_us;
        }
        effect.stats.last_step_us = (uint32_t)(finished_us - effect.step_started_us);
        if (effect.pending && led_matrix_submit(effect.pixels))
        {
            effect.pending = false;
        }
    }
}

void fluid_effect_get_stats(fluid_effect_stats_t *stats)
{
    *stats = effect.stats;
}
