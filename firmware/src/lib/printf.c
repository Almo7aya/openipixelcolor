/* Minimal printf to the console UART: %c %s %d %i %u %x %X %p %%, width, '0' pad, 'l' ignored. */
#include <stdarg.h>
#include <stdint.h>
#include "printf.h"
#include "../hal/uart.h"

static void out(char c) { uart_putc(CONSOLE_UART, c); }

static void out_num(uint32_t v, unsigned base, int neg, int width, char pad, int upper)
{
    char buf[12];
    int i = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do { buf[i++] = digits[v % base]; v /= base; } while (v);
    if (neg) buf[i++] = '-';
    while (i < width) { out(pad); width--; }
    while (i) out(buf[--i]);
}

void kvprintf(const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%') { out(*fmt); continue; }
        fmt++;
        char pad = ' ';
        int width = 0;
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l') fmt++;
        switch (*fmt) {
        case 'c': out((char)va_arg(ap, int)); break;
        case 's': { const char *s = va_arg(ap, const char *); if (!s) s = "(null)";
                    int n = 0; for (const char *p = s; *p; p++) n++;
                    while (n++ < width) out(' ');
                    while (*s) out(*s++); } break;
        case 'd': case 'i': { int32_t v = va_arg(ap, int32_t);
                    out_num(v < 0 ? (uint32_t)-v : (uint32_t)v, 10, v < 0, width, pad, 0); } break;
        case 'u': out_num(va_arg(ap, uint32_t), 10, 0, width, pad, 0); break;
        case 'x': out_num(va_arg(ap, uint32_t), 16, 0, width, pad, 0); break;
        case 'X': out_num(va_arg(ap, uint32_t), 16, 0, width, pad, 1); break;
        case 'p': out('0'); out('x'); out_num(va_arg(ap, uint32_t), 16, 0, 8, '0', 0); break;
        case '%': out('%'); break;
        case 0: return;
        default: out('%'); out(*fmt); break;
        }
    }
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
}
