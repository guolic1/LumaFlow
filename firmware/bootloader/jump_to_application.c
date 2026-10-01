#include "jump_to_application.h"

#include <stdint.h>

#include "main.h"
#include "serial_uart.h"

#define APPLICATION_FLASH_START 0x08002000UL
#define APPLICATION_FLASH_END 0x08010000UL
#define SRAM_START 0x20000000UL
#define SRAM_END 0x20002000UL

__attribute__((naked, noreturn)) static void start_application(uint32_t stack_pointer,
                                                               uint32_t reset_handler)
{
    __asm volatile("msr msp, r0\n"
                   "cpsie i\n"
                   "bx r1\n");
}

bool bootloader_application_is_valid(void)
{
    uint32_t stack_pointer = *(const volatile uint32_t *)APPLICATION_FLASH_START;
    uint32_t reset_handler = *(const volatile uint32_t *)(APPLICATION_FLASH_START + 4U);
    uint32_t reset_address = reset_handler & ~1UL;

    return (stack_pointer >= SRAM_START) && (stack_pointer <= SRAM_END) &&
           ((stack_pointer & 0x7U) == 0U) && ((reset_handler & 1U) != 0U) &&
           (reset_address >= APPLICATION_FLASH_START) && (reset_address < APPLICATION_FLASH_END);
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
