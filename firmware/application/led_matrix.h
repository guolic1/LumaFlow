#ifndef LED_MATRIX_H
#define LED_MATRIX_H

#include <stdbool.h>

#define LED_MATRIX_LED_COUNT 217U

/* Application only: requires 64 MHz SYSCLK/APB and PF2 configured as GPIO in OB.
 * Initialization starts an all-off display and releases PA13/PA14 from SWD.
 * Returns false if already running or the clock/option-byte prerequisites fail.
 * Call all public functions from the main loop, not an interrupt handler. */
bool led_matrix_init(void);
void led_matrix_stop(void);

/* Main-loop producer only. pixels[n - 1] controls schematic LED Dn.
 * Copies all 217 pixels; applies them at the next frame boundary.
 * Returns false for NULL, a stopped driver, or a frame still awaiting display. */
bool led_matrix_submit(const bool pixels[LED_MATRIX_LED_COUNT]);

#endif
