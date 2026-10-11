#include "fluid_sim.h"

#include <stddef.h>
#include <string.h>

#define UNIT 1024
#define WALL 4559
#define MAX_SPEED 768
#define PARTICLE_DISTANCE 680
#define REST_DENSITY 512U
#define LED_PITCH 704
#define LED_ROW_PITCH 610
#define RENDER_RADIUS 800
#define SEPARATION_PASSES 1U

#include "fluid_sim_tables.h"

_Static_assert(FLUID_PARTICLE_COUNT <= UINT8_MAX && FLUID_PARTICLE_COUNT % 16U == 0U,
               "Particle batches must fit the bounded cursor");
_Static_assert(FLUID_GRID_STRIDE == FLUID_GRID_SIZE + 1U, "MAC face guard stride mismatch");

/* Conservative cell coverage of the analytic hexagon; outer cells are solid.
 * The pressure boundary is a staircase approximation, separate from collision planes. */
static const uint8_t open_cells[FLUID_GRID_SIZE * FLUID_GRID_SIZE] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
    0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const int16_t normals[6][2] = {
    {0, 1024}, {0, -1024}, {887, 512}, {887, -512}, {-887, 512}, {-887, -512},
};
static const uint16_t row_start[17] = {
    0, 9, 19, 30, 42, 55, 69, 84, 100, 117, 133, 148, 162, 175, 187, 198, 208,
};

static int32_t absolute(int32_t value)
{
    return value < 0 ? -value : value;
}

static int16_t speed(int32_t value)
{
    if (value > MAX_SPEED)
    {
        value = MAX_SPEED;
    }
    if (value < -MAX_SPEED)
    {
        value = -MAX_SPEED;
    }
    return (int16_t)value;
}

/* Velocity keeps its original 20ms reference unit; integrate over 1/rate seconds. */
static int32_t scale_step(int32_t value)
{
    return value * 50 / (int32_t)FLUID_SIM_RATE_HZ;
}

bool fluid_sim_contains(int16_t x, int16_t y)
{
    for (unsigned i = 0U; i < 6U; ++i)
    {
        int32_t dot = (x - FLUID_CENTER) * normals[i][0] + (y - FLUID_CENTER) * normals[i][1];
        if (dot > (WALL + 4) * UNIT)
        {
            return false;
        }
    }
    return true;
}

static void collide(fluid_particle_t *p)
{
    for (unsigned pass = 0U; pass < 2U; ++pass)
    {
        for (unsigned i = 0U; i < 6U; ++i)
        {
            int32_t nx = normals[i][0];
            int32_t ny = normals[i][1];
            int32_t excess =
                ((p->x - FLUID_CENTER) * nx + (p->y - FLUID_CENTER) * ny) / UNIT - WALL;
            if (excess > 0)
            {
                /* Bias inward by two quantization units at intersecting planes. */
                p->x = (int16_t)(p->x - (excess + 2) * nx / UNIT);
                p->y = (int16_t)(p->y - (excess + 2) * ny / UNIT);
            }
            int32_t outward = (p->vx * nx + p->vy * ny) / UNIT;
            if ((excess >= -2) && (outward > 0))
            {
                p->vx = speed(p->vx - outward * nx / UNIT);
                p->vy = speed(p->vy - outward * ny / UNIT);
            }
        }
    }
}

static bool solid(int x, int y)
{
    return (x < 0) || (y < 0) || (x >= (int)FLUID_GRID_SIZE) || (y >= (int)FLUID_GRID_SIZE) ||
           !open_cells[y * FLUID_GRID_SIZE + x];
}

static bool face_open(unsigned face, unsigned axis)
{
    return face_cells[axis * FLUID_FACE_COUNT + face][0] != 0U;
}

static bool face_liquid(const fluid_sim_t *sim, unsigned face, unsigned axis)
{
    const uint8_t *cells = face_cells[axis * FLUID_FACE_COUNT + face];
    return sim->scratch.cells.type[cells[0]] == FLUID_CELL_LIQUID ||
           sim->scratch.cells.type[cells[1]] == FLUID_CELL_LIQUID;
}

/* Bilinear weights sum exactly to 256. Positions remain inside the guard cells. */
static void stencil(int32_t x, int32_t y, unsigned stride, unsigned indices[4], uint16_t weights[4])
{
    unsigned ix = (unsigned)(x / UNIT);
    unsigned iy = (unsigned)(y / UNIT);
    unsigned fx = (unsigned)(x % UNIT) / 4U;
    unsigned fy = (unsigned)(y % UNIT) / 4U;
    indices[0] = iy * stride + ix;
    indices[1] = indices[0] + 1U;
    indices[2] = indices[0] + stride;
    indices[3] = indices[2] + 1U;
    weights[0] = (uint16_t)((256U - fx) * (256U - fy) / 256U);
    weights[1] = (uint16_t)(fx * (256U - fy) / 256U);
    weights[2] = (uint16_t)((256U - fx) * fy / 256U);
    weights[3] = (uint16_t)(256U - weights[0] - weights[1] - weights[2]);
}

void fluid_sim_init(fluid_sim_t *sim)
{
    memset(sim, 0, sizeof(*sim));
    unsigned count = 0U;
    unsigned row = 0U;
    /* Twice the area per particle preserves the fill level at half the count. */
    for (int y = FLUID_CENTER + WALL - 128; y > UNIT && count < FLUID_PARTICLE_COUNT; y -= 690)
    {
        for (int x = UNIT + (int)(row % 2U) * 398; x < 12 * UNIT && count < FLUID_PARTICLE_COUNT;
             x += 796)
        {
            if (fluid_sim_contains((int16_t)x, (int16_t)y))
            {
                sim->particles[count++] = (fluid_particle_t){(int16_t)x, (int16_t)y, 0, 0};
            }
        }
        ++row;
    }
}

bool fluid_sim_begin(fluid_sim_t *sim, int16_t raw_x, int16_t raw_y)
{
    if ((sim == NULL) || (sim->phase != FLUID_PHASE_IDLE))
    {
        return false;
    }
    /* Specific force has the opposite sign to apparent gravity.
     * Gain is artistic, not a metric length calibration. Scale the impulse with dt. */
    sim->gravity_x = speed(scale_step(-(int32_t)raw_x / 32));
    sim->gravity_y = speed(scale_step(-(int32_t)raw_y / 32));
    sim->phase = FLUID_PHASE_MOVE;
    sim->cursor = 0U;
    sim->iteration = 0U;
    sim->separation_pass = 0U;
    return true;
}

static void separate(fluid_sim_t *sim)
{
    unsigned end = sim->cursor + 8U;
    if (end > FLUID_PARTICLE_COUNT)
    {
        end = FLUID_PARTICLE_COUNT;
    }
    for (unsigned i = sim->cursor; i < end; ++i)
    {
        for (unsigned j = i + 1U; j < FLUID_PARTICLE_COUNT; ++j)
        {
            fluid_particle_t *a = &sim->particles[i];
            fluid_particle_t *b = &sim->particles[j];
            int32_t dx = b->x - a->x;
            int32_t dy = b->y - a->y;
            if ((absolute(dx) >= PARTICLE_DISTANCE) || (absolute(dy) >= PARTICLE_DISTANCE))
            {
                continue;
            }
            /* A max+half-min norm avoids square roots; drift projection also resists packing. */
            int32_t ax = absolute(dx), ay = absolute(dy);
            int32_t distance = ax > ay ? ax + ay / 2 : ay + ax / 2;
            if (distance >= PARTICLE_DISTANCE)
            {
                continue;
            }
            if (distance == 0)
            {
                dx = 1;
                distance = 1;
            }
            int32_t correction = (PARTICLE_DISTANCE - distance) / 2;
            int32_t sx = divide_small(dx * correction, (unsigned)distance);
            int32_t sy = divide_small(dy * correction, (unsigned)distance);
            a->x = (int16_t)(a->x - sx);
            a->y = (int16_t)(a->y - sy);
            b->x = (int16_t)(b->x + sx);
            b->y = (int16_t)(b->y + sy);
        }
    }
    sim->cursor = (uint8_t)end;
    if (end == FLUID_PARTICLE_COUNT)
    {
        for (unsigned i = 0U; i < FLUID_PARTICLE_COUNT; ++i)
        {
            collide(&sim->particles[i]);
        }
        sim->cursor = 0U;
        if (++sim->separation_pass == SEPARATION_PASSES)
        {
            sim->phase = FLUID_PHASE_CLEAR;
        }
    }
}

static void deposit(fluid_sim_t *sim)
{
    unsigned end = sim->cursor + 16U;
    for (unsigned i = sim->cursor; i < end; ++i)
    {
        const fluid_particle_t *p = &sim->particles[i];
        for (unsigned axis = 0U; axis < 2U; ++axis)
        {
            unsigned indices[4];
            uint16_t weights[4];
            stencil(p->x - (axis == 1U ? UNIT / 2 : 0), p->y - (axis == 0U ? UNIT / 2 : 0),
                    FLUID_GRID_STRIDE, indices, weights);
            for (unsigned k = 0U; k < 4U; ++k)
            {
                unsigned face = axis * FLUID_FACE_COUNT + indices[k];
                sim->grid.sum[face] += (axis == 0U ? p->vx : p->vy) * weights[k];
                sim->scratch.weight[face] += weights[k];
            }
        }
    }
    sim->cursor = (uint8_t)end;
    if (end == FLUID_PARTICLE_COUNT)
    {
        sim->phase = FLUID_PHASE_NORMALIZE;
    }
}

static void normalize(fluid_sim_t *sim)
{
    /* Compact low-to-high: each 16-bit result overwrites only already consumed sums. */
    for (unsigned i = 0U; i < 2U * FLUID_FACE_COUNT; ++i)
    {
        int32_t sum = sim->grid.sum[i];
        uint16_t weight = sim->scratch.weight[i];
        sim->grid.velocity.current[i] = weight ? (int16_t)(sum / weight) : 0;
    }
    for (unsigned i = 0U; i < FLUID_GRID_SIZE * FLUID_GRID_SIZE; ++i)
    {
        sim->scratch.cells.type[i] = open_cells[i] ? FLUID_CELL_AIR : FLUID_CELL_SOLID;
        sim->scratch.cells.density[i] = 0U;
    }
    for (unsigned axis = 0U; axis < 2U; ++axis)
    {
        for (unsigned i = 0U; i < FLUID_FACE_COUNT; ++i)
        {
            if (!face_open(i, axis))
            {
                sim->grid.velocity.current[axis * FLUID_FACE_COUNT + i] = 0;
            }
        }
    }
    memcpy(sim->grid.velocity.previous, sim->grid.velocity.current,
           sizeof(sim->grid.velocity.current));
    for (unsigned i = 0U; i < FLUID_PARTICLE_COUNT; ++i)
    {
        const fluid_particle_t *p = &sim->particles[i];
        unsigned cell = (unsigned)(p->y / UNIT) * FLUID_GRID_SIZE + (unsigned)(p->x / UNIT);
        sim->scratch.cells.type[cell] = FLUID_CELL_LIQUID;
        unsigned indices[4];
        uint16_t weights[4];
        stencil(p->x - UNIT / 2, p->y - UNIT / 2, FLUID_GRID_SIZE, indices, weights);
        for (unsigned k = 0U; k < 4U; ++k)
        {
            sim->scratch.cells.density[indices[k]] += weights[k];
        }
    }
    sim->phase = FLUID_PHASE_PROJECT;
}

static void project(fluid_sim_t *sim)
{
    static const uint16_t inverse[5] = {0U, 256U, 128U, 85U, 64U};
    for (unsigned y = 1U; y + 1U < FLUID_GRID_SIZE; ++y)
    {
        for (unsigned x = 1U; x + 1U < FLUID_GRID_SIZE; ++x)
        {
            unsigned cell = y * FLUID_GRID_SIZE + x;
            if (sim->scratch.cells.type[cell] != FLUID_CELL_LIQUID)
            {
                continue;
            }
            unsigned faces[4] = {
                y * FLUID_GRID_STRIDE + x,
                y * FLUID_GRID_STRIDE + x + 1U,
                FLUID_FACE_COUNT + y * FLUID_GRID_STRIDE + x,
                FLUID_FACE_COUNT + (y + 1U) * FLUID_GRID_STRIDE + x,
            };
            unsigned open[4] = {
                !solid((int)x - 1, (int)y),
                !solid((int)x + 1, (int)y),
                !solid((int)x, (int)y - 1),
                !solid((int)x, (int)y + 1),
            };
            unsigned count = open[0] + open[1] + open[2] + open[3];
            int32_t divergence =
                sim->grid.velocity.current[faces[1]] - sim->grid.velocity.current[faces[0]] +
                sim->grid.velocity.current[faces[3]] - sim->grid.velocity.current[faces[2]];
            if (sim->scratch.cells.density[cell] > REST_DENSITY)
            {
                divergence -= (int32_t)(sim->scratch.cells.density[cell] - REST_DENSITY) / 4;
            }
            int32_t correction = -divergence * inverse[count] / 256;
            for (unsigned k = 0U; k < 4U; ++k)
            {
                if (open[k])
                {
                    int32_t sign = (k == 0U || k == 2U) ? -1 : 1;
                    sim->grid.velocity.current[faces[k]] =
                        speed(sim->grid.velocity.current[faces[k]] + sign * correction);
                }
            }
        }
    }
    if (++sim->iteration == FLUID_PRESSURE_ITERATIONS)
    {
        sim->cursor = 0U;
        sim->phase = FLUID_PHASE_TRANSFER;
    }
}

static void transfer(fluid_sim_t *sim)
{
    unsigned end = sim->cursor + 16U;
    for (unsigned i = sim->cursor; i < end; ++i)
    {
        fluid_particle_t *p = &sim->particles[i];
        for (unsigned axis = 0U; axis < 2U; ++axis)
        {
            unsigned indices[4];
            uint16_t weights[4];
            stencil(p->x - (axis == 1U ? UNIT / 2 : 0), p->y - (axis == 0U ? UNIT / 2 : 0),
                    FLUID_GRID_STRIDE, indices, weights);
            int32_t pic_sum = 0, delta_sum = 0;
            unsigned total = 0U;
            for (unsigned k = 0U; k < 4U; ++k)
            {
                if (face_open(indices[k], axis) && face_liquid(sim, indices[k], axis))
                {
                    unsigned face = axis * FLUID_FACE_COUNT + indices[k];
                    pic_sum += sim->grid.velocity.current[face] * weights[k];
                    delta_sum +=
                        (sim->grid.velocity.current[face] - sim->grid.velocity.previous[face]) *
                        weights[k];
                    total += weights[k];
                }
            }
            int16_t *velocity = axis == 0U ? &p->vx : &p->vy;
            if (total != 0U)
            {
                int32_t pic = divide_small(pic_sum, total);
                int32_t flip = *velocity + divide_small(delta_sum, total);
                /* Keep ~98% FLIP for lively splashes; a small PIC share limits noise. */
                *velocity = speed((6 * pic + 250 * flip) / 256);
            }
        }
        collide(p);
    }
    sim->cursor = (uint8_t)end;
    if (end == FLUID_PARTICLE_COUNT)
    {
        sim->phase = FLUID_PHASE_IDLE;
    }
}

bool fluid_sim_advance(fluid_sim_t *sim)
{
    switch (sim->phase)
    {
    case FLUID_PHASE_MOVE:
        for (unsigned i = 0U; i < FLUID_PARTICLE_COUNT; ++i)
        {
            fluid_particle_t *p = &sim->particles[i];
            p->vx = speed(p->vx + sim->gravity_x);
            p->vy = speed(p->vy + sim->gravity_y);
            p->x = (int16_t)(p->x + scale_step(p->vx));
            p->y = (int16_t)(p->y + scale_step(p->vy));
            collide(p);
        }
        sim->phase = FLUID_PHASE_SEPARATE;
        break;
    case FLUID_PHASE_SEPARATE:
        separate(sim);
        break;
    case FLUID_PHASE_CLEAR:
        memset(&sim->grid, 0, sizeof(sim->grid));
        memset(&sim->scratch, 0, sizeof(sim->scratch));
        sim->phase = FLUID_PHASE_DEPOSIT;
        break;
    case FLUID_PHASE_DEPOSIT:
        deposit(sim);
        break;
    case FLUID_PHASE_NORMALIZE:
        normalize(sim);
        break;
    case FLUID_PHASE_PROJECT:
        project(sim);
        break;
    case FLUID_PHASE_TRANSFER:
        transfer(sim);
        return sim->phase == FLUID_PHASE_IDLE;
    case FLUID_PHASE_IDLE:
        break;
    }
    return false;
}

static unsigned row_count(unsigned row)
{
    return 9U + (row <= 8U ? row : 16U - row);
}

bool fluid_sim_led_position(unsigned index, int16_t *x, int16_t *y)
{
    if ((index >= 217U) || (x == NULL) || (y == NULL))
    {
        return false;
    }
    unsigned row = 0U;
    while (row + 1U < 17U && index >= row_start[row + 1U])
    {
        ++row;
    }
    *x = (int16_t)(FLUID_CENTER + (int)(row_count(row) - 1U) * LED_PITCH / 2 -
                   (int)(index - row_start[row]) * LED_PITCH);
    *y = (int16_t)(FLUID_CENTER + (8 - (int)row) * LED_ROW_PITCH);
    return true;
}

bool fluid_sim_render(fluid_sim_t *sim, bool pixels[217])
{
    if ((sim == NULL) || (pixels == NULL) || (sim->phase != FLUID_PHASE_IDLE))
    {
        return false;
    }
    memset(sim->scratch.render, 0, sizeof(sim->scratch.render));
    for (unsigned i = 0U; i < FLUID_PARTICLE_COUNT; ++i)
    {
        const fluid_particle_t *p = &sim->particles[i];
        int nearest_row = divide_small(FLUID_CENTER + 8 * LED_ROW_PITCH - p->y + LED_ROW_PITCH / 2,
                                       LED_ROW_PITCH);
        for (int row = nearest_row - 1; row <= nearest_row + 1; ++row)
        {
            if (row < 0 || row > 16)
            {
                continue;
            }
            int count = (int)row_count((unsigned)row);
            int right_x = FLUID_CENTER + (count - 1) * LED_PITCH / 2;
            int nearest_column = divide_small(right_x - p->x + LED_PITCH / 2, LED_PITCH);
            for (int col = nearest_column - 1; col <= nearest_column + 1; ++col)
            {
                if (col < 0 || col >= count)
                {
                    continue;
                }
                int32_t dx = right_x - col * LED_PITCH - p->x;
                int32_t dy = FLUID_CENTER + (8 - row) * LED_ROW_PITCH - p->y;
                int32_t kernel = RENDER_RADIUS * RENDER_RADIUS - dx * dx - dy * dy;
                if (kernel > 0)
                {
                    unsigned led = row_start[row] + (unsigned)col;
                    sim->scratch.render[led] += (uint16_t)(kernel / 2048);
                }
            }
        }
    }
    for (unsigned i = 0U; i < 217U; ++i)
    {
        pixels[i] = sim->scratch.render[i] >= 180U;
    }
    return true;
}
