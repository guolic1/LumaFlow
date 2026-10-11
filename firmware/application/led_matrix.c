#include "led_matrix.h"

#include "led_matrix_gpio.h"
#include "led_matrix_scan.h"
#include "main.h"

#define LED_MATRIX_TIMER_CLOCK_HZ 64000000UL
#define LED_MATRIX_TIMER_TICKS 3975U
#define LED_MATRIX_GPIO_COUNT 18U
#define LED_MATRIX_PORT_COUNT LED_MATRIX_GPIO_PORT_COUNT

typedef struct
{
    uint8_t port;
    uint8_t pin;
} matrix_pin_t;

static GPIO_TypeDef *const ports[LED_MATRIX_PORT_COUNT] = {GPIOA, GPIOB, GPIOC, GPIOF};
static const matrix_pin_t pins[LED_MATRIX_GPIO_COUNT] = {
    {0U, 2U}, {0U, 3U},  {0U, 4U},  {0U, 5U},  {0U, 6U}, {0U, 7U}, {1U, 0U}, {1U, 1U}, {0U, 8U},
    {2U, 6U}, {0U, 13U}, {0U, 14U}, {0U, 15U}, {1U, 3U}, {1U, 4U}, {1U, 5U}, {3U, 2U}, {1U, 8U},
};

static led_matrix_scan_t scan;
static uint32_t mode_masks[LED_MATRIX_PORT_COUNT];
static bool running;

static void matrix_release(void)
{
    for (unsigned int port = 0U; port < LED_MATRIX_PORT_COUNT; ++port)
    {
        ports[port]->MODER &= ~mode_masks[port];
    }
    __DSB();
}

static void matrix_drive(led_matrix_drive_t drive)
{
    led_matrix_gpio_mask_t high = led_matrix_gpio_mask(drive.high);
    led_matrix_gpio_mask_t low = led_matrix_gpio_mask(drive.low);

    /* Prepare the next group before blanking to keep the dead time short.
     * Preload levels while every matrix pin is high impedance, then enable
     * all sinks before any source. This avoids driving an intermediate pair. */
    matrix_release();
    for (unsigned int port = 0U; port < LED_MATRIX_PORT_COUNT; ++port)
    {
        ports[port]->BSRR = high.levels[port] | (low.levels[port] << 16U);
    }
    for (unsigned int port = 0U; port < LED_MATRIX_PORT_COUNT; ++port)
    {
        ports[port]->MODER |= low.modes[port];
    }
    for (unsigned int port = 0U; port < LED_MATRIX_PORT_COUNT; ++port)
    {
        ports[port]->MODER |= high.modes[port];
    }
}

bool led_matrix_init(void)
{
    if (running || SystemCoreClock != LED_MATRIX_TIMER_CLOCK_HZ ||
        LL_RCC_GetAPB1Prescaler() != LL_RCC_APB1_DIV_1 ||
        (FLASH->OPTR & FLASH_OPTR_NRST_MODE) != FLASH_OPTR_NRST_MODE_1)
    {
        return false;
    }

    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA | LL_IOP_GRP1_PERIPH_GPIOB |
                            LL_IOP_GRP1_PERIPH_GPIOC | LL_IOP_GRP1_PERIPH_GPIOF);
    for (unsigned int index = 0U; index < LED_MATRIX_GPIO_COUNT; ++index)
    {
        const matrix_pin_t *pin = &pins[index];
        mode_masks[pin->port] |= 3UL << (pin->pin * 2U);
    }
    matrix_release();
    for (unsigned int index = 0U; index < LED_MATRIX_GPIO_COUNT; ++index)
    {
        const matrix_pin_t *pin = &pins[index];
        GPIO_TypeDef *port = ports[pin->port];
        port->PUPDR &= ~(3UL << (pin->pin * 2U));
        port->OSPEEDR &= ~(3UL << (pin->pin * 2U));
        port->OTYPER &= ~(1UL << pin->pin);
        port->BSRR = 1UL << (pin->pin + 16U);
    }
    led_matrix_scan_init(&scan);

    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_TIM14);
    LL_APB2_GRP1_ForceReset(LL_APB2_GRP1_PERIPH_TIM14);
    LL_APB2_GRP1_ReleaseReset(LL_APB2_GRP1_PERIPH_TIM14);
    TIM14->PSC = 0U;
    TIM14->ARR = LED_MATRIX_TIMER_TICKS - 1U;
    TIM14->EGR = TIM_EGR_UG;
    TIM14->SR = 0U;
    TIM14->DIER = TIM_DIER_UIE;
    NVIC_ClearPendingIRQ(TIM14_IRQn);
    NVIC_SetPriority(TIM14_IRQn, 0U);
    running = true;
    NVIC_EnableIRQ(TIM14_IRQn);
    TIM14->CR1 = TIM_CR1_CEN;
    return true;
}

bool led_matrix_submit(const bool pixels[LED_MATRIX_LED_COUNT])
{
    return running && led_matrix_scan_submit(&scan, pixels);
}

void led_matrix_stop(void)
{
    if (!running)
    {
        return;
    }
    NVIC_DisableIRQ(TIM14_IRQn);
    __DSB();
    __ISB();
    TIM14->CR1 = 0U;
    TIM14->DIER = 0U;
    TIM14->SR = 0U;
    NVIC_ClearPendingIRQ(TIM14_IRQn);
    matrix_release();
    running = false;
    LL_APB2_GRP1_DisableClock(LL_APB2_GRP1_PERIPH_TIM14);
}

void TIM14_IRQHandler(void)
{
    if ((TIM14->SR & TIM_SR_UIF) != 0U)
    {
        TIM14->SR = 0U;
        matrix_drive(led_matrix_scan_next(&scan));
    }
}
