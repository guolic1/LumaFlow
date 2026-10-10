#include "led_pwm.h"

#include "main.h"

void led_pwm_init(void)
{
    LL_GPIO_InitTypeDef gpio = {0};

    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2);
    LL_APB1_GRP1_ForceReset(LL_APB1_GRP1_PERIPH_TIM2);
    LL_APB1_GRP1_ReleaseReset(LL_APB1_GRP1_PERIPH_TIM2);

    /* App runs at 64 MHz with APB undivided: 1 MHz / 1000 = 1 kHz. */
    TIM2->PSC = 64U - 1U;
    TIM2->ARR = LED_PWM_MAX_DUTY - 1U;
    TIM2->CCR4 = 0U;
    TIM2->CCMR2 = TIM_CCMR2_OC4PE | (6UL << TIM_CCMR2_OC4M_Pos);
    TIM2->CCER = TIM_CCER_CC4E;
    TIM2->CR1 = TIM_CR1_ARPE;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0U;

    /* D1: IO0/PA2 is K, IO1/PA3 is A (TIM2_CH4, AF2). Start dark. */
    LL_GPIO_ResetOutputPin(GPIOA, IO0_Pin | IO1_Pin);
    gpio.Pin = IO0_Pin;
    gpio.Mode = LL_GPIO_MODE_OUTPUT;
    gpio.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
    gpio.Speed = LL_GPIO_SPEED_FREQ_HIGH;
    gpio.Pull = LL_GPIO_PULL_NO;
    LL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = IO1_Pin;
    gpio.Mode = LL_GPIO_MODE_ALTERNATE;
    gpio.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio.Alternate = LL_GPIO_AF_2;
    LL_GPIO_Init(GPIOA, &gpio);

    TIM2->CR1 |= TIM_CR1_CEN;
}

void led_pwm_set_duty(uint16_t duty)
{
    /* Preload applies at the next period, without truncating the current pulse.
     * CCR4 == 0 is always low; CCR4 == ARR + 1 is always high. */
    TIM2->CCR4 = duty;
}

void led_pwm_deinit(void)
{
    /* Release the cathode first, then stop PWM and release the anode. */
    LL_GPIO_SetOutputPin(GPIOA, IO0_Pin | IO1_Pin);
    TIM2->CR1 = 0U;
    TIM2->CCER = 0U;
    LL_GPIO_SetPinOutputType(GPIOA, IO1_Pin, LL_GPIO_OUTPUT_OPENDRAIN);
    LL_GPIO_SetPinMode(GPIOA, IO1_Pin, LL_GPIO_MODE_OUTPUT);
    LL_APB1_GRP1_DisableClock(LL_APB1_GRP1_PERIPH_TIM2);
}
