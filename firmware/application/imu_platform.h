#ifndef IMU_PLATFORM_H
#define IMU_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

bool imu_platform_start(void);
void imu_platform_stop(void);
uint32_t imu_platform_ticks(void);
uint32_t imu_platform_time_ms(void);
bool imu_platform_read(uint8_t reg, uint8_t *data, uint8_t length);
bool imu_platform_write(uint8_t reg, uint8_t value);
void imu_platform_delay_ms(uint32_t duration);

#endif
