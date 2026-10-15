#ifndef IMU_H
#define IMU_H

#include "qma6100p.h"

#define IMU_SAMPLE_RATE_HZ 100U
#define IMU_SLEEP_SAMPLE_RATE_HZ 25U /* Motion detector only; supported: 25, 50, 100. */
#define IMU_RANGE_G QMA6100P_RANGE_G
#define IMU_COUNTS_PER_G QMA6100P_COUNTS_PER_G

typedef enum
{
    IMU_OK = 0,
    IMU_BUS_ERROR = 1,
    IMU_CHIP_ID_ERROR = 2,
    IMU_CONFIG_ERROR = 3,
    IMU_NOT_READY = 4,
    IMU_STOPPED = 5,
} imu_status_t;

typedef struct
{
    qma6100p_sample_t sample;
    uint32_t sequence;
    uint32_t timestamp_ms;
    uint32_t read_errors;
    uint32_t missed_ticks;
    imu_status_t status;
    bool valid;
    bool ready;
} imu_snapshot_t;

typedef struct
{
    uint32_t sleep_entries;
    uint32_t motion_events;
    uint32_t empty_interrupts;
    uint32_t read_errors;
    uint32_t last_event_after_sleep_ms;
    qma6100p_sample_t sample;
    imu_status_t last_status;
    imu_status_t sample_status;
    uint8_t motion_status;
} imu_wake_stats_t;

/* Main-loop-only API; interrupts only flag work, all I2C executes in these APIs. */
void imu_init(void);
void imu_poll(void);
void imu_stop(void);
void imu_get_snapshot(imu_snapshot_t *snapshot);
/* Retained since imu_init. The last event's XYZ is read after the status latch,
 * before restoring 100Hz; it is diagnostic data, not the triggering sample. */
void imu_get_wake_stats(imu_wake_stats_t *stats);
/* Suspend host sampling, leaving the sensor's low-rate motion detector active.
 * The snapshot becomes invalid/IMU_STOPPED until fresh sampling resumes. */
bool imu_set_motion_sleep(bool sleeping, uint16_t threshold_mg);
bool imu_check_motion_wake(bool *motion);

#endif
