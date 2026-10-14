#include "main.h"
#include "adc.h"
#include "commands.h"
#include "dma.h"
#include "fluid_effect.h"
#include "i2c.h"
#include "imu.h"
#include "led_matrix.h"
#include "motion_sleep.h"
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
    /* Keep the existing two Flash wait states; accelerate instruction fetch at 64MHz. */
    LL_FLASH_EnablePrefetch();
    LL_FLASH_EnableInstCache();

    MX_DMA_Init();
    MX_ADC1_Init();
    MX_I2C1_Init();
    serial_uart_init();
    application_commands_init(&command_server);
    if (!led_matrix_init())
    {
        Error_Handler();
    }
    imu_init();
    fluid_effect_init();
    motion_sleep_init();

    while (1)
    {
        imu_poll();
        command_server_poll(&command_server);

        if (application_commands_take_bootloader_request())
        {
            imu_stop();
            led_matrix_stop();
            serial_uart_flush();
            NVIC_SystemReset();
        }
        if (!motion_sleep_poll())
        {
            Error_Handler();
        }
        fluid_effect_poll();
        if (motion_sleep_is_sleeping())
        {
            /* TIM3 and UART/DMA stay clocked in normal Sleep. Keep acquiring the
             * IMU in the main loop after each interrupt; Stop would halt TIM3. */
            LL_LPM_EnableSleep();
            __DSB();
            __WFI();
        }
    }
}
