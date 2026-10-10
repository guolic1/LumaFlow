#include "main.h"
#include "adc.h"
#include "commands.h"
#include "dma.h"
#include "i2c.h"
#include "led_pwm.h"
#include "serial_uart.h"

extern void SystemClock_Config(void);

static void application_matrix_swd_pins_init(void)
{
    LL_GPIO_InitTypeDef gpio = {0};

    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);

    /* The other matrix pins retain their Bootloader configuration. */
    gpio.Pin = IO10_Pin | IO11_Pin;
    LL_GPIO_SetOutputPin(GPIOA, gpio.Pin);
    gpio.Mode = LL_GPIO_MODE_OUTPUT;
    gpio.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
    gpio.Speed = LL_GPIO_SPEED_FREQ_LOW;
    gpio.Pull = LL_GPIO_PULL_NO;
    LL_GPIO_Init(GPIOA, &gpio);
}

int main(void)
{
    static command_server_t command_server;

    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);

    /* SysTick_IRQn interrupt configuration */
    NVIC_SetPriority(SysTick_IRQn, 3);

    LL_SYSCFG_EnablePinRemap(LL_SYSCFG_PIN_RMP_PA11);
    LL_SYSCFG_EnablePinRemap(LL_SYSCFG_PIN_RMP_PA12);

    SystemClock_Config();

    application_matrix_swd_pins_init();
    led_pwm_init();
    MX_DMA_Init();
    MX_ADC1_Init();
    MX_I2C1_Init();
    serial_uart_init();
    application_commands_init(&command_server);

    while (1)
    {
        command_server_poll(&command_server);

        if (application_commands_take_bootloader_request())
        {
            led_pwm_deinit();
            serial_uart_flush();
            NVIC_SystemReset();
        }
    }
}
