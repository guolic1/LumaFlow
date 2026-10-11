#ifndef FLUID_EFFECT_H
#define FLUID_EFFECT_H

#include <stdint.h>

#define FLUID_EFFECT_RATE_HZ 50U

typedef struct
{
    uint32_t completed_steps;
    uint32_t last_work_us;
    uint32_t max_work_us;
    uint32_t last_step_us;
    uint32_t missed_steps;
    uint32_t max_slice_us;
    uint16_t state_bytes;
} fluid_effect_stats_t;

/* Main-loop only; the timer and display retain their existing ownership. */
void fluid_effect_init(void);
void fluid_effect_poll(void);
void fluid_effect_get_stats(fluid_effect_stats_t *stats);

#endif
