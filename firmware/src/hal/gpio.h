/* GPIO: flat register file at 0x40010000, 13 words per port (docs/HARDWARE.md).
 * reg 0 IN, 1 OUT, 5 input-enable, 6 output-enable, 9/10 pull (which is up/down unknown). */
#pragma once
#include <stdint.h>
#include "regs.h"

enum { GPIO_P0 = 0, GPIO_P1 = 1, GPIO_P2 = 2 };
enum { GPIO_R_IN = 0, GPIO_R_OUT = 1, GPIO_R_IE = 5, GPIO_R_OE = 6, GPIO_R_PULL9 = 9, GPIO_R_PULL10 = 10 };

#define GPIO_REG(port, reg) REG32(0x40010000u + ((uint32_t)(port) * 13u + (uint32_t)(reg)) * 4u)

static inline void gpio_set_bits(unsigned port, unsigned reg, uint32_t m) { GPIO_REG(port, reg) |= m; }
static inline void gpio_clr_bits(unsigned port, unsigned reg, uint32_t m) { GPIO_REG(port, reg) &= ~m; }

static inline void gpio_output(unsigned port, unsigned pin, int level)
{
    uint32_t m = 1u << pin;
    if (level) gpio_set_bits(port, GPIO_R_OUT, m); else gpio_clr_bits(port, GPIO_R_OUT, m);
    gpio_clr_bits(port, GPIO_R_IE, m);
    gpio_set_bits(port, GPIO_R_OE, m);
}

static inline void gpio_input(unsigned port, unsigned pin)
{
    uint32_t m = 1u << pin;
    gpio_set_bits(port, GPIO_R_IE, m);
    gpio_clr_bits(port, GPIO_R_OE, m);
}

static inline void gpio_write(unsigned port, unsigned pin, int level)
{
    if (level) gpio_set_bits(port, GPIO_R_OUT, 1u << pin); else gpio_clr_bits(port, GPIO_R_OUT, 1u << pin);
}

static inline int gpio_read(unsigned port, unsigned pin)
{
    return (GPIO_REG(port, GPIO_R_IN) >> pin) & 1u;
}
