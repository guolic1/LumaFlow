#ifndef MOTION_SLEEP_CONFIG_H
#define MOTION_SLEEP_CONFIG_H

#define MOTION_SLEEP_IDLE_MS 30000U
/* +/-2g IMU raw units: 4096 = 1g. Compare each axis against the last motion
 * reference, not zero: a stationary board still measures gravity in any orientation. */
#define MOTION_SLEEP_QUIET_THRESHOLD_RAW 205 /* ~0.05g; at or above this restarts the timer */
#define MOTION_SLEEP_WAKE_THRESHOLD_RAW 328  /* ~0.08g; higher threshold provides hysteresis */
#define MOTION_SLEEP_MAX_SAMPLE_GAP_MS 100U

#endif
