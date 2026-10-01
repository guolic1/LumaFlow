#ifndef SERIAL_UART_H
#define SERIAL_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void serial_uart_init(void);
void serial_uart_deinit(void);
bool serial_uart_read_byte(void *context, uint8_t *byte);
void serial_uart_write(void *context, const uint8_t *data, size_t length);
void serial_uart_flush(void);

#endif
