/* UART driver (UART0 / UART1). */
#include "uart.h"
#include "regs.h"
#include "sys.h"

/* UART0 0x40005000, UART1 0x40006000 (console), UART2 0x40007000 (replies to the Wi-Fi/BLE module) */
static uint32_t uart_base(unsigned n) { return UART0_BASE + (uint32_t)n * 0x1000u; }

/* route UARTn pins (sel picks one of two pin options) */
void uart_pinmux(unsigned n, unsigned sel)
{
    uint32_t v = REG32(UART_PINMUX);
    sel &= 1u;
    switch (n) {
    case 0: v = (v & ~(1u << 10)) | (sel << 10) | (1u << 9);  v = (v & ~(1u << 8))  | (sel << 8)  | (1u << 7);  break;
    case 1: v = (v & ~(1u << 18)) | (sel << 18) | (1u << 17); v = (v & ~(1u << 16)) | (sel << 16) | (1u << 15); break;
    case 2: v = (v & ~(1u << 26)) | (sel << 26) | (1u << 25); v = (v & ~(1u << 24)) | (sel << 24) | (1u << 23); break;
    default: return;
    }
    REG32(UART_PINMUX) = v;
}

void uart_init(unsigned n, uint32_t baud)       /* 8N1 */
{
    uint32_t b    = uart_base(n);
    uint32_t gate = 1u << (11u + n);      /* 11/12/13 */
    uint32_t rst  = 1u << (4u + n);       /* 4/5/6 */

    REG8(b + 0x0C) &= 0xFEu;
    REG8(b + 0x1C) &= 0xFEu;
    REG32(CLK_GATE0) &= ~gate;

    REG8(b + 0x00) = (uint8_t)((REG8(b + 0x00) & 0xF8u) | 3u);       /* 8 data bits, no parity */
    REG8(b + 0x00) = (uint8_t)((REG8(b + 0x00) & 0xEFu) | 0x20u);    /* 1 stop bit, enable */

    uint32_t clk = sys_apb_hz();
    uint32_t d   = baud << 3;
    REG32(b + 0x04) = ((clk / d - 1u) & 0xFFFFu) | (((((clk % d) << 3) / d) & 7u) << 16);

    REG32(CLK_GATE0) |= gate;
    REG8(b + 0x0C) = (uint8_t)((REG8(b + 0x0C) & 0x55u) | 1u);
    REG8(b + 0x1C) = (uint8_t)((REG8(b + 0x1C) & 0xF5u) | 1u);
    REG8(b + 0x20) &= 0xF3u;
    REG8(b + 0x28) &= 0xE0u;
    REG32(RST_CTRL) = (REG32(RST_CTRL) & ~rst) | rst;
}

/* every wait is bounded: a dead/unclocked UART must never hang the firmware (recoverability) */
#define UART_SPIN 200000u

static int wait_bit(uint32_t reg, unsigned bit)
{
    for (uint32_t n = 0; n < UART_SPIN; n++)
        if ((REG32(reg) >> bit) & 1u) return 0;
    return -1;
}

void uart_putc(unsigned n, char c)
{
    uint32_t b = uart_base(n);
    if (c == '\n') uart_putc(n, '\r');
    if (wait_bit(b + 0x14, 9)) return;
    REG32(b + 0x18) = (uint8_t)c;
    wait_bit(b + 0x14, 8);
    REG8(b + 0x1C) = (uint8_t)((REG8(b + 0x1C) & 0xF7u) | 4u);
}

void uart_puts(unsigned n, const char *s)
{
    while (*s) uart_putc(n, *s++);
}

/* raw bytes, no newline translation; returns once the last byte is in the FIFO */
void uart_write(unsigned n, const void *buf, unsigned len)
{
    uint32_t b = uart_base(n);
    const uint8_t *p = buf;
    REG8(b + 0x1C) &= 0xF7u;
    while (len--) {
        if (wait_bit(b + 0x14, 9)) return;
        REG32(b + 0x18) = *p++;
    }
}
