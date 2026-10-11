#ifndef LED_MATRIX_SCAN_H
#define LED_MATRIX_SCAN_H

#include <stdatomic.h>
#include <stdint.h>

#include "led_matrix.h"

#define LED_MATRIX_SLOT_COUNT 161U

/* Internal scan state; one main-loop producer and one timer-interrupt consumer. */
typedef struct
{
    bool frames[2][LED_MATRIX_LED_COUNT];
    atomic_bool pending;
    uint16_t slot;
    uint8_t active_frame;
} led_matrix_scan_t;

typedef struct
{
    uint32_t high;
    /* Includes IO17's discharge clamp whenever IO17 is not a source. */
    uint32_t low;
} led_matrix_drive_t;

void led_matrix_scan_init(led_matrix_scan_t *scan);
bool led_matrix_scan_submit(led_matrix_scan_t *scan, const bool pixels[LED_MATRIX_LED_COUNT]);
led_matrix_drive_t led_matrix_scan_next(led_matrix_scan_t *scan);

#endif
