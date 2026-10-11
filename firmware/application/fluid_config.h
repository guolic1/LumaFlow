#ifndef FLUID_CONFIG_H
#define FLUID_CONFIG_H

/* Rebuild the application after tuning. Q8 coefficients use 256 = 100%.
 * Geometry is calibrated to this board; grid size and fixed-point scale are structural. */
#define FLUID_UNIT 1024
#define FLUID_WALL 4559
#define FLUID_MAX_SPEED 16384
#define FLUID_LED_PITCH 704
#define FLUID_LED_ROW_PITCH 610

/* Timing and fill. More particles/iterations cost CPU time and possibly RAM. */
#define FLUID_SIM_RATE_HZ 25U
#define FLUID_PARTICLE_COUNT 64U
#define FLUID_PRESSURE_ITERATIONS 12U
#define FLUID_INITIAL_X_PITCH 796
#define FLUID_INITIAL_ROW_PITCH 690
#define FLUID_INITIAL_WALL_INSET 128

/* Input: smaller divisor = stronger force; larger filter coefficient = faster response. */
#ifndef FLUID_ACCEL_DIVISOR
#define FLUID_ACCEL_DIVISOR 32
#endif
#ifndef FLUID_INPUT_FILTER_Q8
#define FLUID_INPUT_FILTER_Q8 128
#endif

/* Larger FLIP share preserves more inertia. Density correction divisor works inversely. */
#ifndef FLUID_FLIP_Q8
#define FLUID_FLIP_Q8 250
#endif
#ifndef FLUID_DENSITY_CORRECTION_DIVISOR
#define FLUID_DENSITY_CORRECTION_DIVISOR 4
#endif
#define FLUID_PARTICLE_DISTANCE 680
#define FLUID_REST_DENSITY 512U
#define FLUID_SEPARATION_PASSES 1U

/* Render radius is a kernel support, not a collision radius. Smaller thresholds
 * thicken the lit area. Keep radius <= 1.5 times both LED pitches (3x3 stencil). */
#define FLUID_RENDER_RADIUS 800
#ifndef FLUID_RENDER_THRESHOLD
#define FLUID_RENDER_THRESHOLD 180U
#endif
#ifndef FLUID_RENDER_WEIGHT_DIVISOR
#define FLUID_RENDER_WEIGHT_DIVISOR 2048
#endif

#endif
