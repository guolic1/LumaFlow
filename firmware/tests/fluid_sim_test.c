#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fluid_sim.h"
#include "fluid_sim_tables.h"

static fluid_sim_t sim;

static void test_step_duration(void)
{
    fluid_sim_init(&sim);
    sim.particles[0] = (fluid_particle_t){FLUID_CENTER, FLUID_CENTER, 90, -60};
    assert(fluid_sim_begin(&sim, 0, 0));
    assert(!fluid_sim_advance(&sim));
    /* Velocities retain their 20ms reference unit; a 25Hz step spans two units. */
    assert(sim.particles[0].x == FLUID_CENTER + 180);
    assert(sim.particles[0].y == FLUID_CENTER - 120);

    fluid_sim_init(&sim);
    sim.particles[0] = (fluid_particle_t){FLUID_CENTER, FLUID_CENTER, 0, 0};
    assert(fluid_sim_begin(&sim, -4096, 4096));
    assert(!fluid_sim_advance(&sim));
    assert(sim.particles[0].vx == 256 && sim.particles[0].vy == -256);
    assert(sim.particles[0].x == FLUID_CENTER + 512);
    assert(sim.particles[0].y == FLUID_CENTER - 512);

    fluid_sim_init(&sim);
    sim.particles[0] = (fluid_particle_t){FLUID_CENTER, FLUID_CENTER, 0, 0};
    assert(fluid_sim_begin(&sim, -8192, 8191));
    assert(!fluid_sim_advance(&sim));
    assert(sim.particles[0].vx == 512 && sim.particles[0].vy == -510);
    assert(sim.particles[0].x == FLUID_CENTER + 1024);
    assert(sim.particles[0].y == FLUID_CENTER - 1020);

    fluid_sim_init(&sim);
    sim.particles[0] = (fluid_particle_t){FLUID_CENTER, FLUID_CENTER, 768, -768};
    assert(fluid_sim_begin(&sim, 0, 0));
    assert(!fluid_sim_advance(&sim));
    assert(sim.particles[0].vx == 768 && sim.particles[0].vy == -768);
    assert(sim.particles[0].x == FLUID_CENTER + 1536);
    assert(sim.particles[0].y == FLUID_CENTER - 1536);
}

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
    /* Faster particle velocities also widen FLIP's delta quotient to +/-1536.
     * Its interpolation weights sum to at most 256, keeping numerators below 2^20. */
    for (unsigned d = 1U; d <= 256U; ++d)
    {
        for (int32_t q = 1025; q <= 1536; ++q)
        {
            int32_t value = q * (int32_t)d + (int32_t)d - 1;
            assert(divide_small(value, d) == value / (int32_t)d);
            assert(divide_small(-value, d) == -value / (int32_t)d);
        }
    }
    /* PIC and FLIP interpolation now reach 16384 and 32768 respectively. */
    const int32_t quotients[] = {1536, 4095, 4096, 16384, 32768};
    for (unsigned d = 1U; d <= 256U; ++d)
    {
        for (unsigned i = 0U; i < sizeof(quotients) / sizeof(quotients[0]); ++i)
        {
            int32_t value = quotients[i] * (int32_t)d + (int32_t)d - 1;
            assert(divide_small(value, d) == value / (int32_t)d);
            assert(divide_small(-value, d) == -value / (int32_t)d);
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
        assert(abs(p->vx) <= 768 && abs(p->vy) <= 768);
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

static void test_ballistic_droplet(void)
{
    fluid_sim_init(&sim);
    sim.particles[0] = (fluid_particle_t){FLUID_CENTER, 3584, 240, -180};
    assert(fluid_sim_begin(&sim, 0, 0));
    while (sim.phase != FLUID_PHASE_TRANSFER)
    {
        assert(!fluid_sim_advance(&sim));
    }
    unsigned cell = (unsigned)(sim.particles[0].y / 1024) * FLUID_GRID_SIZE +
                    (unsigned)(sim.particles[0].x / 1024);
    assert(sim.scratch.cells.density[cell] <= 256U);
    while (!fluid_sim_advance(&sim))
    {
    }
    /* A detached droplet keeps its momentum instead of being damped by grid pressure. */
    assert(sim.particles[0].vx == 240 && sim.particles[0].vy == -180);
    assert(sim.particles[0].x == FLUID_CENTER + 480 && sim.particles[0].y == 3224);
    bool pixels[217];
    assert(fluid_sim_render(&sim, pixels));
    int32_t nearest_distance = INT32_MAX;
    unsigned nearest_led = 0U;
    for (unsigned led = 0U; led < 217U; ++led)
    {
        int16_t x, y;
        assert(fluid_sim_led_position(led, &x, &y));
        int32_t dx = x - sim.particles[0].x, dy = y - sim.particles[0].y;
        int32_t distance = dx * dx + dy * dy;
        if (distance < nearest_distance)
        {
            nearest_distance = distance;
            nearest_led = led;
        }
    }
    assert(pixels[nearest_led]);
}

static void test_splash_momentum(void)
{
    fluid_sim_init(&sim);
    /* Two nearby droplets share grid faces, which smooths their opposing velocities. */
    sim.particles[0] = (fluid_particle_t){FLUID_CENTER - 600, 3584, 200, 0};
    sim.particles[1] = (fluid_particle_t){FLUID_CENTER + 600, 3584, -200, 0};
    step(0, 0);
    assert(sim.particles[0].vx >= 150 && sim.particles[1].vx <= -150);
}

static void test_droplet_outline(void)
{
    fluid_sim_init(&sim);
    for (unsigned i = 1U; i < FLUID_PARTICLE_COUNT; ++i)
    {
        sim.particles[i] = (fluid_particle_t){FLUID_CENTER, 10500, 0, 0};
    }
    /* A droplet between two LEDs lights them without bridging to the next row. */
    sim.particles[0] = (fluid_particle_t){FLUID_CENTER + 352, FLUID_CENTER, 0, 0};
    bool pixels[217];
    assert(fluid_sim_render(&sim, pixels));
    assert(pixels[107] && pixels[108] && !pixels[124]);
}

int main(void)
{
    test_droplet_outline();
    test_splash_momentum();
    test_ballistic_droplet();
    test_step_duration();
    test_numeric_tables();
    bool pixels[217];
    int16_t x, y;
    assert(sizeof(sim) <= 3072U);
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
        assert(pixels[reference_leds[k]]);
        unsigned lit_count = 0U;
        for (unsigned i = 0U; i < 217U; ++i)
        {
            if (pixels[i])
            {
                int16_t led_x, led_y;
                assert(fluid_sim_led_position(i, &led_x, &led_y));
                int32_t dx = led_x - x, dy = led_y - y;
                assert(dx * dx + dy * dy < 900 * 900);
                ++lit_count;
            }
        }
        assert(lit_count <= 7U);
    }
    printf("PIC/FLIP tests passed; state=%zu bytes, particles=%u, grid=%ux%u\n", sizeof(sim),
           FLUID_PARTICLE_COUNT, FLUID_GRID_SIZE, FLUID_GRID_SIZE);
    return 0;
}
