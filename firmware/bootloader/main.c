#include "main.h"

#include "commands.h"
#include "dma.h"
#include "jump_to_application.h"
#include "serial_uart.h"

#define BOOTLOADER_COMMAND_WINDOW_MS 3000U

extern void SystemClock_Config(void);

static void bootloader_gpio_init(void)
{
    LL_GPIO_InitTypeDef gpio = {0};

    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA | LL_IOP_GRP1_PERIPH_GPIOB |
                           LL_IOP_GRP1_PERIPH_GPIOC | LL_IOP_GRP1_PERIPH_GPIOF);

    /* Change only the pulls on the two matrix pins shared with SWD. */
    LL_GPIO_SetPinPull(IO10_GPIO_Port, IO10_Pin, LL_GPIO_PULL_NO);
    LL_GPIO_SetPinPull(IO11_GPIO_Port, IO11_Pin, LL_GPIO_PULL_NO);

    /* Release the remaining matrix pins without changing USART1 pins. */
    gpio.Mode = LL_GPIO_MODE_INPUT;
    gpio.Pull = LL_GPIO_PULL_NO;
    gpio.Pin = IO0_Pin | IO1_Pin | IO2_Pin | IO3_Pin | IO4_Pin | IO5_Pin |
               IO8_Pin | IO12_Pin;
    LL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = IO6_Pin | IO7_Pin | IO13_Pin | IO14_Pin | IO15_Pin | IO17_Pin;
    LL_GPIO_Init(GPIOB, &gpio);

    gpio.Pin = IO9_Pin;
    LL_GPIO_Init(GPIOC, &gpio);

    /* PF2 is usable as IO16 only when NRST_MODE is configured for GPIO. */
    gpio.Pin = IO16_Pin;
    LL_GPIO_Init(GPIOF, &gpio);
}

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

    bootloader_gpio_init();
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
