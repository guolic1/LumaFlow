#include "main.h"

#include "commands.h"
#include "dma.h"
#include "jump_to_application.h"
#include "serial_uart.h"

#define BOOTLOADER_COMMAND_WINDOW_MS 200U

extern void SystemClock_Config(void);

static bool wait_for_command(command_server_t *server, uint32_t timeout_ms)
{
    uint32_t elapsed_ms = 0U;

    while (elapsed_ms < timeout_ms)
    {
        if (command_server_poll(server))
        {
            return true;
        }

        if ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) != 0U)
        {
            ++elapsed_ms;
        }
    }

    return false;
}

int main(void)
{
    static command_server_t command_server;
    bool stay_in_bootloader;

    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);

    /* SysTick_IRQn interrupt configuration */
    NVIC_SetPriority(SysTick_IRQn, 3);

    LL_SYSCFG_EnablePinRemap(LL_SYSCFG_PIN_RMP_PA11);
    LL_SYSCFG_EnablePinRemap(LL_SYSCFG_PIN_RMP_PA12);

    SystemClock_Config();

    MX_DMA_Init();
    serial_uart_init();
    bootloader_commands_init(&command_server);

    stay_in_bootloader = !bootloader_application_is_valid();
    if (!stay_in_bootloader)
    {
        stay_in_bootloader = wait_for_command(&command_server, BOOTLOADER_COMMAND_WINDOW_MS);
    }

    if (!stay_in_bootloader)
    {
        bootloader_jump_to_application();
    }

    while (1)
    {
        command_server_poll(&command_server);

        if (bootloader_commands_take_jump_request())
        {
            serial_uart_flush();
            bootloader_jump_to_application();
        }
    }
}
