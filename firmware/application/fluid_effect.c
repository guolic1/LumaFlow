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
    int32_t input_sum_x;
    int32_t input_sum_y;
    uint32_t input_samples;
    uint32_t input_sequence;
    bool input_valid;
    bool pending;
} fluid_effect_t;

static fluid_effect_t effect;

_Static_assert(sizeof(fluid_effect_t) <= 3840U, "Fluid storage exceeds its RAM budget");
_Static_assert(FLUID_EFFECT_RATE_HZ == FLUID_SIM_RATE_HZ,
               "Fluid scheduling and integration must use the same rate");
_Static_assert(IMU_SAMPLE_RATE_HZ % FLUID_EFFECT_RATE_HZ == 0U,
               "Fluid scheduling requires an integer number of IMU ticks per step");

#define IMU_TICKS_PER_STEP (IMU_SAMPLE_RATE_HZ / FLUID_EFFECT_RATE_HZ)

_Static_assert(FLUID_INPUT_FILTER_Q8 >= 0 && FLUID_INPUT_FILTER_Q8 <= 256,
               "Input filter coefficient must be in 0..256");

static void sample_input(const imu_snapshot_t *snapshot, bool healthy)
{
    if (!healthy)
    {
        effect.input_valid = false;
        effect.input_samples = 0U;
        effect.input_sum_x = 0;
        effect.input_sum_y = 0;
        return;
    }
    if (effect.input_valid && snapshot->sequence == effect.input_sequence)
    {
        return;
    }
    if (!effect.input_valid)
    {
        effect.filtered_x = snapshot->sample.x;
        effect.filtered_y = snapshot->sample.y;
    }
    else
    {
        /* Filter at the 100Hz sample rate, not the 25Hz physics rate. */
        effect.filtered_x += ((int32_t)snapshot->sample.x - effect.filtered_x) *
                             (int32_t)FLUID_INPUT_FILTER_Q8 / 256;
        effect.filtered_y += ((int32_t)snapshot->sample.y - effect.filtered_y) *
                             (int32_t)FLUID_INPUT_FILTER_Q8 / 256;
    }
    effect.input_sequence = snapshot->sequence;
    effect.input_valid = true;
    /* A delayed scheduler must not allow unbounded sums. Normal windows have four samples. */
    if (effect.input_samples < IMU_SAMPLE_RATE_HZ)
    {
        effect.input_sum_x += effect.filtered_x;
        effect.input_sum_y += effect.filtered_y;
        ++effect.input_samples;
    }
}

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
    sample_input(&snapshot, healthy);
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
            /* Average the force over this window so short shakes contribute an impulse
             * without multiplying it by the number of IMU samples. */
            int32_t input_x = effect.input_samples != 0U
                                  ? effect.input_sum_x / (int32_t)effect.input_samples
                                  : effect.filtered_x;
            int32_t input_y = effect.input_samples != 0U
                                  ? effect.input_sum_y / (int32_t)effect.input_samples
                                  : effect.filtered_y;
            (void)fluid_sim_begin(&effect.sim, (int16_t)input_x, (int16_t)input_y);
            effect.step_started_us = imu_platform_time_us();
            effect.work_us = 0U;
        }
        else
        {
            ++effect.stats.missed_steps;
        }
        /* Discard an expired window even when busy; never replay stale shake forces. */
        effect.input_sum_x = 0;
        effect.input_sum_y = 0;
        effect.input_samples = 0U;
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
