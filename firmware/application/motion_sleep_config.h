#ifndef MOTION_SLEEP_CONFIG_H
#define MOTION_SLEEP_CONFIG_H

#include "imu.h"

#define MOTION_SLEEP_IDLE_MS 30000U
/* Thresholds in mg; round up to native counts (512 and 2048 at +/-8g).
 * Compare each axis against the last motion reference, not zero/gravity. */
#define MOTION_SLEEP_QUIET_THRESHOLD_MG 500U
#define MOTION_SLEEP_WAKE_THRESHOLD_MG 2000U
#define MOTION_SLEEP_QUIET_THRESHOLD_RAW                                                           \
    ((MOTION_SLEEP_QUIET_THRESHOLD_MG * IMU_COUNTS_PER_G + 999U) / 1000U)
#define MOTION_SLEEP_WAKE_THRESHOLD_RAW                                                            \
    ((MOTION_SLEEP_WAKE_THRESHOLD_MG * IMU_COUNTS_PER_G + 999U) / 1000U)
#define MOTION_SLEEP_MAX_SAMPLE_GAP_MS 100U

#endif
