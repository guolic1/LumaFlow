#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fluid_effect.h"
#include "fluid_sim.h"
#include "imu.h"
#include "imu_platform.h"
#include "led_matrix.h"

static fluid_sim_t sim;
static imu_snapshot_t sample;
static uint32_t ticks, microseconds;
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
    memcpy(displayed, pixels, sizeof(displayed));
    return true;
}

static void test_motion_controls(void)
{
    fluid_sim_init(&sim);
    sim.particles[0] = (fluid_particle_t){FLUID_CENTER, FLUID_CENTER, 512, -256};
    assert(fluid_sim_begin(&sim, -4096, 4096));
    assert(sim.gravity_x == 512 && sim.gravity_y == -512); /* doubled input gain */
    assert(!fluid_sim_advance(&sim));
    assert(sim.particles[0].vx == (FLUID_VELOCITY_RETAIN_Q8 == 256 ? 1024 : 512));
    assert(sim.particles[0].vy == (FLUID_VELOCITY_RETAIN_Q8 == 256 ? -768 : -512));
    assert(sim.particles[0].x == FLUID_CENTER + 2 * sim.particles[0].vx);
    assert(sim.particles[0].y == FLUID_CENTER + 2 * sim.particles[0].vy);
}

static void test_wall_control(void)
{
    fluid_sim_init(&sim);
    sim.particles[0] = (fluid_particle_t){FLUID_CENTER, FLUID_CENTER + FLUID_WALL - 1, 100, 400};
    /* Isolate collision from drag and grid interpolation. */
    sim.phase = FLUID_PHASE_TRANSFER;
    assert(!fluid_sim_advance(&sim));
    assert(sim.particles[0].vx == 100);
    assert(sim.particles[0].vy == (FLUID_WALL_RESTITUTION_Q8 == 0 ? 0 : -400));
}

static void test_cohesion_control(void)
{
    fluid_sim_init(&sim);
    for (unsigned i = 2U; i < FLUID_PARTICLE_COUNT; ++i)
    {
        sim.particles[i] = (fluid_particle_t){FLUID_CENTER, 10500, 0, 0};
    }
    sim.particles[0] = (fluid_particle_t){FLUID_CENTER - 500, 3584, 0, 0};
    sim.particles[1] = (fluid_particle_t){FLUID_CENTER + 500, 3584, 0, 0};
    sim.phase = FLUID_PHASE_SEPARATE;
    while (sim.separation_pass == 0U)
    {
        assert(!fluid_sim_advance(&sim));
    }
    int16_t first_pass = sim.particles[0].vx;
    while (sim.phase == FLUID_PHASE_SEPARATE)
    {
        assert(!fluid_sim_advance(&sim));
    }
    assert(sim.particles[0].vx == first_pass); /* extra solver passes do not multiply cohesion */
    assert(sim.particles[0].vx == -sim.particles[1].vx);
    assert(FLUID_SURFACE_TENSION_Q8 == 0 ? first_pass == 0 : first_pass > 0);
    assert(sim.particles[0].vy == 0 && sim.particles[1].vy == 0);
}

static void test_flip_control(void)
{
    fluid_sim_init(&sim);
    for (unsigned i = 0U; i < FLUID_PARTICLE_COUNT; ++i)
    {
        sim.particles[i] = (fluid_particle_t){FLUID_CENTER, FLUID_CENTER, 300, 300};
    }
    memset(sim.scratch.cells.type, FLUID_CELL_LIQUID, sizeof(sim.scratch.cells.type));
    for (unsigned face = 0U; face < 2U * FLUID_FACE_COUNT; ++face)
    {
        sim.grid.velocity.current[face] = 200;
        sim.grid.velocity.previous[face] = 0;
    }
    sim.phase = FLUID_PHASE_TRANSFER;
    while (!fluid_sim_advance(&sim))
    {
    }
    /* PIC = 200, FLIP = 500: half FLIP gives 350, pure FLIP gives 500. */
    assert(sim.particles[0].vx == (FLUID_FLIP_Q8 == 128 ? 350 : 500));
    assert(sim.particles[0].vy == sim.particles[0].vx);
}

static void test_density_control(void)
{
    fluid_sim_init(&sim);
    unsigned cell = 6U * FLUID_GRID_SIZE + 6U;
    sim.scratch.cells.type[cell] = FLUID_CELL_LIQUID;
    sim.scratch.cells.density[cell] = FLUID_REST_DENSITY + 160U;
    sim.phase = FLUID_PHASE_PROJECT;
    assert(!fluid_sim_advance(&sim));
    unsigned left = 6U * FLUID_GRID_STRIDE + 6U;
    int expected = FLUID_DENSITY_CORRECTION_DIVISOR == 2 ? 20 : 40;
    assert(sim.grid.velocity.current[left] == -expected);
    assert(sim.grid.velocity.current[left + 1U] == expected);
}

static void test_render_controls(void)
{
    fluid_sim_init(&sim);
    for (unsigned i = 1U; i < FLUID_PARTICLE_COUNT; ++i)
    {
        sim.particles[i] = (fluid_particle_t){FLUID_CENTER, 10500, 0, 0};
    }
    sim.particles[0] = (fluid_particle_t){FLUID_CENTER + 352, FLUID_CENTER, 0, 0};
    bool pixels[217];
    assert(fluid_sim_render(&sim, pixels));
    assert(pixels[108] == (FLUID_RENDER_THRESHOLD < 600U));
    sim.particles[0].x = FLUID_CENTER + 128;
    assert(fluid_sim_render(&sim, pixels));
    assert(pixels[108]); /* 609 at divisor 1024; only 304 if that control is ignored */
}

static void test_filter_control(void)
{
    ticks = 0U;
    sample = (imu_snapshot_t){0};
    sample.ready = true;
    sample.valid = true;
    sample.status = IMU_OK;
    sample.sequence = 1U;
    fluid_effect_init();
    fluid_effect_poll(); /* Seed the filter with zero and include that first sample. */
    for (ticks = 1U; ticks <= 4U; ++ticks)
    {
        sample.sequence = ticks + 1U;
        sample.timestamp_ms = imu_platform_time_ms();
        sample.sample.x = -8192;
        for (unsigned poll = 0U; poll < 60U; ++poll)
        {
            fluid_effect_poll();
        }
    }
    fluid_effect_stats_t stats;
    fluid_effect_get_stats(&stats);
    assert(stats.completed_steps == 1U && stats.missed_steps == 0U);
    /* With no filter response all inputs remain zero. With instant response the
     * five-sample first window averages (0 - 4*8192)/5, truncating towards zero. */
    fluid_sim_init(&sim);
    assert(fluid_sim_begin(&sim, FLUID_INPUT_FILTER_Q8 == 0 ? 0 : -6553, 0));
    while (!fluid_sim_advance(&sim))
    {
    }
    bool expected[217];
    assert(fluid_sim_render(&sim, expected));
    assert(memcmp(displayed, expected, sizeof(expected)) == 0);
}

static void test_extreme_pipeline(void)
{
    fluid_sim_init(&sim);
    for (unsigned i = 0U; i < FLUID_PARTICLE_COUNT; ++i)
    {
        sim.particles[i].vx = i % 2U ? FLUID_MAX_SPEED : -FLUID_MAX_SPEED;
        sim.particles[i].vy = i % 4U < 2U ? FLUID_MAX_SPEED : -FLUID_MAX_SPEED;
    }
    for (unsigned frame = 0U; frame < 100U; ++frame)
    {
        assert(fluid_sim_begin(&sim, frame % 2U ? 8191 : -8192, frame % 4U < 2U ? 8191 : -8192));
        unsigned slices = 0U;
        while (!fluid_sim_advance(&sim))
        {
            assert(++slices < 100U);
        }
        for (unsigned i = 0U; i < FLUID_PARTICLE_COUNT; ++i)
        {
            assert(fluid_sim_contains(sim.particles[i].x, sim.particles[i].y));
            assert(abs(sim.particles[i].vx) <= FLUID_MAX_SPEED);
            assert(abs(sim.particles[i].vy) <= FLUID_MAX_SPEED);
        }
        bool pixels[217];
        assert(fluid_sim_render(&sim, pixels));
    }
}

int main(void)
{
    test_motion_controls();
    test_wall_control();
    test_cohesion_control();
    test_flip_control();
    test_density_control();
    test_render_controls();
    test_filter_control();
    test_extreme_pipeline();
    puts("Fluid coefficient controls and limits passed");
    return 0;
}
