#ifndef MOTION_SLEEP_H
#define MOTION_SLEEP_H

#include <stdbool.h>

/* Initialize once after the LED driver and fluid effect. Main-loop-only APIs.
 * Low-rate IMU motion detection remains active; CPU sampling and UART are stopped. */
void motion_sleep_init(void);
/* False means an IMU transition or LED restart failed; the caller must handle it. */
bool motion_sleep_poll(void);
bool motion_sleep_is_sleeping(void);

#endif
