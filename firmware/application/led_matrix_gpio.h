#ifndef LED_MATRIX_GPIO_H
#define LED_MATRIX_GPIO_H

#include <stdint.h>

#define LED_MATRIX_GPIO_PORT_COUNT 4U

typedef struct
{
    uint32_t levels[LED_MATRIX_GPIO_PORT_COUNT];
    uint32_t modes[LED_MATRIX_GPIO_PORT_COUNT];
} led_matrix_gpio_mask_t;

/* GPIO output mode is 01: spread each pin bit into its two-bit MODER field. */
static inline uint32_t led_matrix_gpio_modes(uint32_t pins)
{
    pins = (pins | (pins << 8U)) & 0x00FF00FFUL;
    pins = (pins | (pins << 4U)) & 0x0F0F0F0FUL;
    pins = (pins | (pins << 2U)) & 0x33333333UL;
    return (pins | (pins << 1U)) & 0x55555555UL;
}

/* Fixed wiring lets the 16.1kHz ISR map all IOs without an 18-pin search. */
static inline led_matrix_gpio_mask_t led_matrix_gpio_mask(uint32_t ios)
{
    led_matrix_gpio_mask_t mask;
    /* Ports A, B, C, F; keep in sync with the initialization pins in led_matrix.c. */
    mask.levels[0] = ((ios & 0x3FUL) << 2U) | (ios & 0x100UL) | ((ios & 0x1C00UL) << 3U);
    mask.levels[1] = ((ios & 0xC0UL) >> 6U) | ((ios & 0xE000UL) >> 10U) | ((ios & 0x20000UL) >> 9U);
    mask.levels[2] = (ios & 0x200UL) >> 3U;
    mask.levels[3] = (ios & 0x10000UL) >> 14U;
    mask.modes[0] = led_matrix_gpio_modes(mask.levels[0]);
    mask.modes[1] = led_matrix_gpio_modes(mask.levels[1]);
    mask.modes[2] = mask.levels[2] << 6U;
    mask.modes[3] = mask.levels[3] << 2U;
    return mask;
}

#endif
