#pragma once
#include <stdint.h>

#define CONSOLE_UART  1u          /* debug console: UART1, 1 Mbaud 8N1 (as the vendor firmware) */
#define CONSOLE_BAUD  1000000u

void uart_pinmux(unsigned n, unsigned sel);
void uart_init(unsigned n, uint32_t baud);
void uart_putc(unsigned n, char c);
void uart_puts(unsigned n, const char *s);
void uart_write(unsigned n, const void *buf, unsigned len);
