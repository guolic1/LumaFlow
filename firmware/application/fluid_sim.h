#ifndef FLUID_SIM_H
#define FLUID_SIM_H

#include <stdbool.h>
#include <stdint.h>

#define FLUID_GRID_SIZE 13U
#define FLUID_GRID_STRIDE 14U
#define FLUID_FACE_COUNT (FLUID_GRID_STRIDE * FLUID_GRID_STRIDE)
#define FLUID_PARTICLE_COUNT 128U
#define FLUID_CENTER 6656
#define FLUID_PRESSURE_ITERATIONS 12U
#define FLUID_CELL_AIR 0U
#define FLUID_CELL_LIQUID 1U
#define FLUID_CELL_SOLID 2U

typedef struct
{
    int16_t x, y, vx, vy;
} fluid_particle_t;

typedef enum
{
    FLUID_PHASE_IDLE,
    FLUID_PHASE_MOVE,
    FLUID_PHASE_SEPARATE,
    FLUID_PHASE_CLEAR,
    FLUID_PHASE_DEPOSIT,
    FLUID_PHASE_NORMALIZE,
    FLUID_PHASE_PROJECT,
    FLUID_PHASE_TRANSFER,
} fluid_phase_t;

/* All storage belongs to one main-loop producer. Scratch members have disjoint
 * lifetimes; render is allowed only between steps and consumes the cell workspace. */
typedef struct
{
    fluid_particle_t particles[FLUID_PARTICLE_COUNT];
    union
    {
        int32_t sum[2U * FLUID_FACE_COUNT];
        struct
        {
            int16_t current[2U * FLUID_FACE_COUNT];
            int16_t previous[2U * FLUID_FACE_COUNT];
        } velocity;
    } grid;
    union
    {
        uint16_t weight[2U * FLUID_FACE_COUNT];
        struct
        {
            uint8_t type[FLUID_GRID_SIZE * FLUID_GRID_SIZE];
            uint16_t density[FLUID_GRID_SIZE * FLUID_GRID_SIZE];
        } cells;
        uint16_t render[217];
    } scratch;
    fluid_phase_t phase;
    uint8_t cursor;
    uint8_t iteration;
    uint8_t separation_pass;
    int16_t gravity_x;
    int16_t gravity_y;
} fluid_sim_t;

void fluid_sim_init(fluid_sim_t *sim);
bool fluid_sim_begin(fluid_sim_t *sim, int16_t raw_x, int16_t raw_y);
/* One bounded slice; true only when an active step just completed. */
bool fluid_sim_advance(fluid_sim_t *sim);
bool fluid_sim_render(fluid_sim_t *sim, bool pixels[217]);
bool fluid_sim_contains(int16_t x, int16_t y);
bool fluid_sim_led_position(unsigned index, int16_t *x, int16_t *y);

#endif
