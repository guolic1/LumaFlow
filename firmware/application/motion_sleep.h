#ifndef MOTION_SLEEP_H
#define MOTION_SLEEP_H

#include <stdbool.h>

/* Initialize once after the LED driver and fluid effect. Main-loop-only APIs.
 * IMU sampling and serial service remain active while the display/solver sleep. */
void motion_sleep_init(void);
/* False means the LED driver could not restart; the caller must handle the failure. */
bool motion_sleep_poll(void);
bool motion_sleep_is_sleeping(void);

#endif
