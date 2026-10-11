#ifndef IMU_H
#define IMU_H

#include "qma6100p.h"

#define IMU_SAMPLE_RATE_HZ 100U
#define IMU_RANGE_G 2U

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

/* Main-loop-only API; TIM3 only schedules work, I2C executes in imu_poll(). */
void imu_init(void);
void imu_poll(void);
void imu_stop(void);
void imu_get_snapshot(imu_snapshot_t *snapshot);

#endif
