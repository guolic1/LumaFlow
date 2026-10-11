#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fluid_sim.h"
#include "fluid_sim_tables.h"

static fluid_sim_t sim;

static bool open_cell(int x, int y)
{
    return x > 0 && x < 12 && y > 0 && y < 12 && abs((y - 6) * 1024) <= 5602 &&
           887 * abs((x - 6) * 1024) + 512 * abs((y - 6) * 1024) <= 5602 * 1024;
}

static void test_numeric_tables(void)
{
    for (unsigned axis = 0U; axis < 2U; ++axis)
    {
        for (int y = 0; y < 14; ++y)
        {
            for (int x = 0; x < 14; ++x)
            {
                int ax = x - (axis == 0U), ay = y - (axis == 1U);
                const uint8_t *cells = face_cells[axis * FLUID_FACE_COUNT + y * 14 + x];
                bool open = open_cell(ax, ay) && open_cell(x, y);
                assert(cells[0] == (open ? ay * 13 + ax : 0));
                assert(cells[1] == (open ? y * 13 + x : 0));
            }
        }
    }
    /* Quotient boundaries, large remainders and both signs across the caller range. */
    for (unsigned d = 1U; d <= 704U; ++d)
    {
        for (int32_t q = 0; q <= 1024; ++q)
        {
            int32_t values[] = {q * (int32_t)d, q * (int32_t)d - 1,
                                q * (int32_t)d + (int32_t)d - 1};
            for (unsigned k = 0U; k < 3U; ++k)
            {
                int32_t value = values[k];
                assert(divide_small(value, d) == value / (int32_t)d);
                assert(divide_small(-value, d) == -value / (int32_t)d);
            }
        }
    }
}

static void step(int16_t ax, int16_t ay)
{
    assert(fluid_sim_begin(&sim, ax, ay));
    unsigned slices = 0U;
    while (!fluid_sim_advance(&sim))
    {
        assert(++slices < 100U);
    }
    for (unsigned i = 0U; i < FLUID_PARTICLE_COUNT; ++i)
    {
        const fluid_particle_t *p = &sim.particles[i];
        assert(fluid_sim_contains(p->x, p->y));
        assert(abs(p->vx) <= 512 && abs(p->vy) <= 512);
    }
}

static int64_t center_x(void)
{
    int64_t total = 0;
    for (unsigned i = 0U; i < FLUID_PARTICLE_COUNT; ++i)
    {
        total += sim.particles[i].x;
    }
    return total;
}

static int64_t center_y(void)
{
    int64_t total = 0;
    for (unsigned i = 0U; i < FLUID_PARTICLE_COUNT; ++i)
    {
        total += sim.particles[i].y;
    }
    return total;
}

static int32_t divergence(void)
{
    int32_t total = 0;
    for (unsigned y = 0U; y < FLUID_GRID_SIZE; ++y)
    {
        for (unsigned x = 0U; x < FLUID_GRID_SIZE; ++x)
        {
            if (sim.scratch.cells.type[y * FLUID_GRID_SIZE + x] == FLUID_CELL_LIQUID)
            {
                unsigned i = y * FLUID_GRID_STRIDE + x;
                total += abs(sim.grid.velocity.current[i + 1U] - sim.grid.velocity.current[i] +
                             sim.grid.velocity.current[FLUID_FACE_COUNT + i + FLUID_GRID_STRIDE] -
                             sim.grid.velocity.current[FLUID_FACE_COUNT + i]);
            }
        }
    }
    return total;
}

int main(void)
{
    test_numeric_tables();
    bool pixels[217];
    int16_t x, y;
    assert(sizeof(sim) <= 3584U);
    assert(fluid_sim_led_position(0U, &x, &y) && x > FLUID_CENTER && y > FLUID_CENTER);
    assert(fluid_sim_led_position(8U, &x, &y) && x < FLUID_CENTER && y > FLUID_CENTER);
    assert(fluid_sim_led_position(108U, &x, &y) && x == FLUID_CENTER && y == FLUID_CENTER);
    assert(fluid_sim_led_position(216U, &x, &y) && x < FLUID_CENTER && y < FLUID_CENTER);
    assert(!fluid_sim_led_position(217U, &x, &y));

    fluid_sim_init(&sim);
    for (unsigned i = 0U; i < FLUID_PARTICLE_COUNT; ++i)
    {
        assert(fluid_sim_contains(sim.particles[i].x, sim.particles[i].y));
        for (unsigned j = i + 1U; j < FLUID_PARTICLE_COUNT; ++j)
        {
            assert(sim.particles[i].x != sim.particles[j].x ||
                   sim.particles[i].y != sim.particles[j].y);
        }
    }
    assert(fluid_sim_render(&sim, pixels));
    unsigned lit = 0U;
    for (unsigned i = 0U; i < 217U; ++i)
    {
        lit += pixels[i] ? 1U : 0U;
    }
    assert(lit > 30U && lit < 160U);
    assert(fluid_sim_begin(&sim, 0, 0));
    assert(!fluid_sim_begin(&sim, 0, 0));
    assert(!fluid_sim_render(&sim, pixels));
    unsigned slices = 0U;
    while (!fluid_sim_advance(&sim))
    {
        assert(++slices < 100U);
    }
    assert(slices > 12U);
    fluid_sim_init(&sim);
    for (unsigned i = 0U; i < FLUID_PARTICLE_COUNT; ++i)
    {
        sim.particles[i].vx = (int16_t)((sim.particles[i].x - FLUID_CENTER) / 12);
        sim.particles[i].vy = (int16_t)((sim.particles[i].y - FLUID_CENTER) / 12);
    }
    assert(fluid_sim_begin(&sim, 0, 0));
    while (sim.phase != FLUID_PHASE_PROJECT)
    {
        assert(!fluid_sim_advance(&sim));
    }
    /* Isolate incompressibility from the separately applied density-drift target. */
    for (unsigned i = 0U; i < FLUID_GRID_SIZE * FLUID_GRID_SIZE; ++i)
    {
        sim.scratch.cells.density[i] = 0U;
    }
    int32_t before = divergence();
    assert(before > 0);
    while (sim.phase == FLUID_PHASE_PROJECT)
    {
        assert(!fluid_sim_advance(&sim));
    }
    assert(divergence() < before / 2);
    while (!fluid_sim_advance(&sim))
    {
    }

    fluid_sim_init(&sim);
    for (unsigned i = 0U; i < 120U; ++i)
    {
        step(-4096, 0);
    }
    int64_t right = center_x();
    fluid_sim_init(&sim);
    for (unsigned i = 0U; i < 120U; ++i)
    {
        step(4096, 0);
    }
    assert(right - center_x() > (int64_t)FLUID_PARTICLE_COUNT * 1024);
    fluid_sim_init(&sim);
    for (unsigned i = 0U; i < 120U; ++i)
    {
        step(0, -4096);
    }
    int64_t down = center_y();
    fluid_sim_init(&sim);
    for (unsigned i = 0U; i < 120U; ++i)
    {
        step(0, 4096);
    }
    assert(down - center_y() > (int64_t)FLUID_PARTICLE_COUNT * 1024);
    for (unsigned i = 0U; i < 600U; ++i)
    {
        static const int16_t gravity[][2] = {
            {-8192, 8191}, {8191, -8192}, {0, 4096}, {4096, 0}, {0, -4096}, {-4096, 0},
        };
        step(gravity[(i / 50U) % 6U][0], gravity[(i / 50U) % 6U][1]);
        assert(fluid_sim_render(&sim, pixels));
    }
    /* A packed cluster exercises separation and the largest transfer accumulators. */
    fluid_sim_init(&sim);
    for (unsigned i = 0U; i < FLUID_PARTICLE_COUNT; ++i)
    {
        sim.particles[i] = (fluid_particle_t){FLUID_CENTER, FLUID_CENTER, 0, 0};
    }
    for (unsigned i = 0U; i < 30U; ++i)
    {
        step(8191, -8192);
    }
    const unsigned reference_leds[] = {0U, 8U, 108U, 216U};
    for (unsigned k = 0U; k < sizeof(reference_leds) / sizeof(reference_leds[0]); ++k)
    {
        assert(fluid_sim_led_position(reference_leds[k], &x, &y));
        for (unsigned i = 0U; i < FLUID_PARTICLE_COUNT; ++i)
        {
            sim.particles[i] = (fluid_particle_t){x, y, 0, 0};
        }
        assert(fluid_sim_render(&sim, pixels));
        for (unsigned i = 0U; i < 217U; ++i)
        {
            assert(pixels[i] == (i == reference_leds[k]));
        }
    }
    printf("PIC/FLIP tests passed; state=%zu bytes, particles=%u, grid=%ux%u\n", sizeof(sim),
           FLUID_PARTICLE_COUNT, FLUID_GRID_SIZE, FLUID_GRID_SIZE);
    return 0;
}
