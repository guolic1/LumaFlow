#ifndef IMU_PLATFORM_H
#define IMU_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

bool imu_platform_start(void);
void imu_platform_stop(void);
void imu_platform_set_sampling(bool enabled);
void imu_platform_enable_motion_wake(bool enabled);
bool imu_platform_motion_pending(void);
void imu_platform_clear_motion(void);
/* Stop1, no periodic wake; restores the 64MHz system clock before returning. */
bool imu_platform_enter_stop(void);
uint32_t imu_platform_ticks(void);
uint32_t imu_platform_time_ms(void);
/* Work/timeout clock excludes suspended TIM3 time; millisecond timestamps include Stop. */
uint32_t imu_platform_time_us(void);
bool imu_platform_read(uint8_t reg, uint8_t *data, uint8_t length);
bool imu_platform_write(uint8_t reg, uint8_t value);
void imu_platform_delay_ms(uint32_t duration);

#endif
