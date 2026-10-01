#include "main.h"
#include "adc.h"
#include "commands.h"
#include "dma.h"
#include "i2c.h"
#include "gpio.h"
#include "serial_uart.h"

extern void SystemClock_Config(void);

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

    MX_GPIO_Init();
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
            serial_uart_flush();
            NVIC_SystemReset();
        }
    }
}
