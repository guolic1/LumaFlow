#ifndef FLUID_EFFECT_H
#define FLUID_EFFECT_H

#include <stdbool.h>
#include <stdint.h>

#include "fluid_config.h"

#define FLUID_EFFECT_RATE_HZ FLUID_SIM_RATE_HZ

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
/* Preserve the liquid and pending frame; resumed scheduling excludes time asleep. */
void fluid_effect_set_paused(bool paused);
void fluid_effect_get_stats(fluid_effect_stats_t *stats);

#endif
