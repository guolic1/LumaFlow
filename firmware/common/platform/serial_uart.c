#include "serial_uart.h"

#include <stdint.h>

#include "dma.h"
#include "main.h"
#include "usart.h"

#define UART_RX_BUFFER_SIZE 256U
#define UART_RX_BUFFER_MASK (UART_RX_BUFFER_SIZE - 1U)
#define UART_RX_DMA_BUFFER_SIZE 256U
#define UART_RX_DMA_BUFFER_MASK (UART_RX_DMA_BUFFER_SIZE - 1U)
#define UART_TX_DMA_BUFFER_SIZE 256U

static uint8_t uart_rx_buffer[UART_RX_BUFFER_SIZE];
static uint8_t uart_rx_dma_buffer[UART_RX_DMA_BUFFER_SIZE];
static uint8_t uart_tx_dma_buffer[UART_TX_DMA_BUFFER_SIZE];
static volatile uint16_t uart_rx_head;
static volatile uint16_t uart_rx_tail;
static uint16_t uart_rx_dma_position;
static volatile bool uart_tx_busy;

_Static_assert((UART_RX_BUFFER_SIZE & UART_RX_BUFFER_MASK) == 0U,
               "UART RX buffer size must be a power of two");
_Static_assert((UART_RX_DMA_BUFFER_SIZE & UART_RX_DMA_BUFFER_MASK) == 0U,
               "UART RX DMA buffer size must be a power of two");

static void uart_rx_push(uint8_t byte)
{
    uint16_t head = uart_rx_head;
    uint16_t next = (uint16_t)((head + 1U) & UART_RX_BUFFER_MASK);

    if (next != uart_rx_tail)
    {
        uart_rx_buffer[head] = byte;
        uart_rx_head = next;
    }
}

static void uart_rx_commit_until(uint16_t position)
{
    while (uart_rx_dma_position != position)
    {
        uart_rx_push(uart_rx_dma_buffer[uart_rx_dma_position]);
        uart_rx_dma_position = (uint16_t)((uart_rx_dma_position + 1U) & UART_RX_DMA_BUFFER_MASK);
    }
}

static void uart_rx_commit_current_position(void)
{
    uint16_t position =
        (uint16_t)(UART_RX_DMA_BUFFER_SIZE - LL_DMA_GetDataLength(DMA1, LL_DMA_CHANNEL_1));

    if (position == UART_RX_DMA_BUFFER_SIZE)
    {
        position = 0U;
    }
    uart_rx_commit_until(position);
}

static void uart_rx_service_dma_flags(void)
{
    if ((LL_DMA_IsActiveFlag_HT1(DMA1) != 0U) &&
        (LL_DMA_IsEnabledIT_HT(DMA1, LL_DMA_CHANNEL_1) != 0U))
    {
        LL_DMA_ClearFlag_HT1(DMA1);
        uart_rx_commit_until(UART_RX_DMA_BUFFER_SIZE / 2U);
    }

    if ((LL_DMA_IsActiveFlag_TC1(DMA1) != 0U) &&
        (LL_DMA_IsEnabledIT_TC(DMA1, LL_DMA_CHANNEL_1) != 0U))
    {
        LL_DMA_ClearFlag_TC1(DMA1);
        uart_rx_commit_until(0U);
    }
}

static void uart_rx_restart_dma(void)
{
    LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_1);
    LL_DMA_ClearFlag_GI1(DMA1);
    uart_rx_dma_position = 0U;
    LL_DMA_SetMemoryAddress(DMA1, LL_DMA_CHANNEL_1, (uint32_t)(uintptr_t)uart_rx_dma_buffer);
    LL_DMA_SetDataLength(DMA1, LL_DMA_CHANNEL_1, UART_RX_DMA_BUFFER_SIZE);
    LL_DMA_EnableChannel(DMA1, LL_DMA_CHANNEL_1);
}

static void uart_tx_start(const uint8_t *data, uint16_t length)
{
    for (uint16_t index = 0U; index < length; ++index)
    {
        uart_tx_dma_buffer[index] = data[index];
    }

    LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_2);
    LL_DMA_ClearFlag_GI2(DMA1);
    LL_DMA_SetMemoryAddress(DMA1, LL_DMA_CHANNEL_2, (uint32_t)(uintptr_t)uart_tx_dma_buffer);
    LL_DMA_SetDataLength(DMA1, LL_DMA_CHANNEL_2, length);
    uart_tx_busy = true;
    __DMB();
    LL_DMA_EnableChannel(DMA1, LL_DMA_CHANNEL_2);
}

void serial_uart_init(void)
{
    uart_rx_head = 0U;
    uart_rx_tail = 0U;
    uart_rx_dma_position = 0U;
    uart_tx_busy = false;

    MX_USART1_UART_Init();

    LL_DMA_SetPeriphAddress(DMA1, LL_DMA_CHANNEL_1,
                            LL_USART_DMA_GetRegAddr(USART1, LL_USART_DMA_REG_DATA_RECEIVE));
    LL_DMA_SetMemoryAddress(DMA1, LL_DMA_CHANNEL_1, (uint32_t)(uintptr_t)uart_rx_dma_buffer);
    LL_DMA_SetDataLength(DMA1, LL_DMA_CHANNEL_1, UART_RX_DMA_BUFFER_SIZE);
    LL_DMA_ClearFlag_GI1(DMA1);
    LL_DMA_EnableIT_HT(DMA1, LL_DMA_CHANNEL_1);
    LL_DMA_EnableIT_TC(DMA1, LL_DMA_CHANNEL_1);
    LL_DMA_EnableIT_TE(DMA1, LL_DMA_CHANNEL_1);

    LL_DMA_SetPeriphAddress(DMA1, LL_DMA_CHANNEL_2,
                            LL_USART_DMA_GetRegAddr(USART1, LL_USART_DMA_REG_DATA_TRANSMIT));
    LL_DMA_ClearFlag_GI2(DMA1);
    LL_DMA_EnableIT_TC(DMA1, LL_DMA_CHANNEL_2);
    LL_DMA_EnableIT_TE(DMA1, LL_DMA_CHANNEL_2);

    LL_USART_ClearFlag_IDLE(USART1);
    LL_USART_EnableIT_ERROR(USART1);
    LL_USART_EnableIT_IDLE(USART1);
    LL_DMA_EnableChannel(DMA1, LL_DMA_CHANNEL_1);
    LL_USART_EnableDMAReq_RX(USART1);
    LL_USART_EnableDMAReq_TX(USART1);
}

void serial_uart_deinit(void)
{
    LL_USART_DisableIT_IDLE(USART1);
    LL_USART_DisableIT_ERROR(USART1);
    LL_USART_DisableDMAReq_RX(USART1);
    LL_USART_DisableDMAReq_TX(USART1);

    LL_DMA_DisableIT_HT(DMA1, LL_DMA_CHANNEL_1);
    LL_DMA_DisableIT_TC(DMA1, LL_DMA_CHANNEL_1);
    LL_DMA_DisableIT_TE(DMA1, LL_DMA_CHANNEL_1);
    LL_DMA_DisableIT_TC(DMA1, LL_DMA_CHANNEL_2);
    LL_DMA_DisableIT_TE(DMA1, LL_DMA_CHANNEL_2);
    LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_1);
    LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_2);
    LL_DMA_ClearFlag_GI1(DMA1);
    LL_DMA_ClearFlag_GI2(DMA1);
    uart_tx_busy = false;

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

    while (length != 0U)
    {
        uint16_t chunk_length =
            (length > UART_TX_DMA_BUFFER_SIZE) ? UART_TX_DMA_BUFFER_SIZE : (uint16_t)length;

        while (uart_tx_busy)
        {
        }

        uart_tx_start(data, chunk_length);
        data += chunk_length;
        length -= chunk_length;
    }
}

void serial_uart_flush(void)
{
    while (uart_tx_busy)
    {
    }

    while (LL_USART_IsActiveFlag_TC(USART1) == 0U)
    {
    }
}

void USART1_IRQHandler(void)
{
    if ((LL_USART_IsActiveFlag_IDLE(USART1) != 0U) && (LL_USART_IsEnabledIT_IDLE(USART1) != 0U))
    {
        LL_USART_ClearFlag_IDLE(USART1);
        uart_rx_service_dma_flags();
        uart_rx_commit_current_position();
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

void DMA1_Channel1_IRQHandler(void)
{
    if ((LL_DMA_IsActiveFlag_TE1(DMA1) != 0U) &&
        (LL_DMA_IsEnabledIT_TE(DMA1, LL_DMA_CHANNEL_1) != 0U))
    {
        LL_DMA_ClearFlag_TE1(DMA1);
        uart_rx_restart_dma();
        return;
    }

    uart_rx_service_dma_flags();
}

void DMA1_Channel2_3_IRQHandler(void)
{
    if ((LL_DMA_IsActiveFlag_TE2(DMA1) != 0U) &&
        (LL_DMA_IsEnabledIT_TE(DMA1, LL_DMA_CHANNEL_2) != 0U))
    {
        LL_DMA_ClearFlag_TE2(DMA1);
        LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_2);
        uart_tx_busy = false;
        return;
    }

    if ((LL_DMA_IsActiveFlag_TC2(DMA1) != 0U) &&
        (LL_DMA_IsEnabledIT_TC(DMA1, LL_DMA_CHANNEL_2) != 0U))
    {
        LL_DMA_ClearFlag_TC2(DMA1);
        LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_2);
        uart_tx_busy = false;
    }
}
