#include "jump_to_application.h"

#include <stdint.h>

#include "application_image.h"
#include "main.h"
#include "serial_uart.h"

__attribute__((naked, noreturn)) static void start_application(uint32_t stack_pointer,
                                                               uint32_t reset_handler)
{
    __asm volatile("msr msp, r0\n"
                   "cpsie i\n"
                   "bx r1\n");
}

bool bootloader_application_is_valid(void)
{
    return application_image_is_valid();
}

void bootloader_jump_to_application(void)
{
    uint32_t stack_pointer;
    uint32_t reset_handler;

    if (!bootloader_application_is_valid())
    {
        return;
    }

    stack_pointer = *(const volatile uint32_t *)APPLICATION_FLASH_START;
    reset_handler = *(const volatile uint32_t *)(APPLICATION_FLASH_START + 4U);

    __disable_irq();
    serial_uart_deinit();

    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;

    NVIC->ICER[0] = 0xFFFFFFFFUL;
    NVIC->ICPR[0] = 0xFFFFFFFFUL;

    LL_RCC_HSI_Enable();
    while (LL_RCC_HSI_IsReady() == 0U)
    {
    }
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSI);
    while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_HSI)
    {
    }
    LL_RCC_PLL_Disable();
    while (LL_RCC_PLL_IsReady() != 0U)
    {
    }

    SCB->VTOR = APPLICATION_FLASH_START;
    __DSB();
    __ISB();
    start_application(stack_pointer, reset_handler);
}
