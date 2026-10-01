#include "serial_uart.h"

#include "main.h"
#include "usart.h"

#define UART_RX_BUFFER_SIZE 256U
#define UART_RX_BUFFER_MASK (UART_RX_BUFFER_SIZE - 1U)

static uint8_t uart_rx_buffer[UART_RX_BUFFER_SIZE];
static volatile uint16_t uart_rx_head;
static volatile uint16_t uart_rx_tail;

void serial_uart_init(void)
{
    uart_rx_head = 0U;
    uart_rx_tail = 0U;

    MX_USART1_UART_Init();

    NVIC_SetPriority(USART1_IRQn, 2U);
    NVIC_ClearPendingIRQ(USART1_IRQn);
    NVIC_EnableIRQ(USART1_IRQn);
    LL_USART_EnableIT_ERROR(USART1);
    LL_USART_EnableIT_RXNE(USART1);
}

void serial_uart_deinit(void)
{
    LL_USART_DisableIT_RXNE(USART1);
    LL_USART_DisableIT_ERROR(USART1);
    NVIC_DisableIRQ(USART1_IRQn);
    NVIC_ClearPendingIRQ(USART1_IRQn);
    LL_USART_Disable(USART1);
}

bool serial_uart_read_byte(void *context, uint8_t *byte)
{
    uint16_t tail;

    (void)context;

    tail = uart_rx_tail;
    if (tail == uart_rx_head)
    {
        return false;
    }

    *byte = uart_rx_buffer[tail];
    uart_rx_tail = (uint16_t)((tail + 1U) & UART_RX_BUFFER_MASK);
    return true;
}

void serial_uart_write(void *context, const uint8_t *data, size_t length)
{
    (void)context;

    for (size_t index = 0U; index < length; ++index)
    {
        while (LL_USART_IsActiveFlag_TXE(USART1) == 0U)
        {
        }
        LL_USART_TransmitData8(USART1, data[index]);
    }
}

void serial_uart_flush(void)
{
    while (LL_USART_IsActiveFlag_TC(USART1) == 0U)
    {
    }
}

void USART1_IRQHandler(void)
{
    if (LL_USART_IsActiveFlag_RXNE(USART1) != 0U)
    {
        uint8_t byte = LL_USART_ReceiveData8(USART1);
        uint16_t head = uart_rx_head;
        uint16_t next = (uint16_t)((head + 1U) & UART_RX_BUFFER_MASK);

        if (next != uart_rx_tail)
        {
            uart_rx_buffer[head] = byte;
            uart_rx_head = next;
        }
    }

    if (LL_USART_IsActiveFlag_ORE(USART1) != 0U)
    {
        LL_USART_ClearFlag_ORE(USART1);
    }
    if (LL_USART_IsActiveFlag_FE(USART1) != 0U)
    {
        LL_USART_ClearFlag_FE(USART1);
    }
    if (LL_USART_IsActiveFlag_NE(USART1) != 0U)
    {
        LL_USART_ClearFlag_NE(USART1);
    }
}
