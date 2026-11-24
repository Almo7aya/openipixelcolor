/* Register access + peripheral addresses (see docs/HARDWARE.md). */
#pragma once
#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define REG16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define REG8(a)  (*(volatile uint8_t  *)(uintptr_t)(a))

/* --- system / clock control ------------------------------------------------------------- */
#define PMU_BASE          0x40020000u   /* always-on block (I) */
#define PMU_WDT_A         (PMU_BASE + 0x04)   /* written 1 once at boot - probably WDT disable (I) */
#define PMU_WDT_B         (PMU_BASE + 0x0C)
#define PMU_XTAL_CFG      (PMU_BASE + 0x28)
#define PMU_XTAL_READY    (PMU_BASE + 0x2C)   /* != 0 when crystal stable (I) */

#define CLK_DIV_TIMER     0x40021000u   /* timer clock divider-1 when CLK_SEL bit2 (C) */
#define CLK_DIV_32K       0x40021010u
#define CLK_SEL           0x4002101Cu   /* bit0 sys=PLL, bit2 timer div, bit3, bit5 APB=PLL/8 (C) */
#define CLK_GATE0         0x40021024u   /* bit11 UART0, bit12 UART1, ... (C) */
#define CLK_GATE1         0x40021028u
#define RST_CTRL          0x40022004u   /* bit2 flash, bit4 UART0, bit5 UART1 (C) */
#define SYS_RESET         0x40022008u   /* write 1 -> chip reset (C) */

#define PLL_CTRL          0x40026000u
#define PLL_REFDIV        0x40026004u
#define PLL_FBDIV         0x40026008u
#define PLL_CFG0C         0x4002600Cu
#define PLL_CFG1C         0x4002601Cu
#define PLL_STATUS        0x40026034u   /* bit0 locked */
#define PLL_CFG44         0x40026044u

#define ANA_APPLY         0x4002E800u   /* write 0 after each 0x4002E8xx change (I) */
#define ANA_E804          0x4002E804u
#define ANA_E808          0x4002E808u
#define ANA_E810          0x4002E810u
#define ANA_XTAL          0x4002E840u

#define MEMCTL_BASE       0x40009000u   /* memory/cache region controller (I) */
#define MEMCTL_CFG        0x40010128u

/* --- peripherals ------------------------------------------------------------------------ */
#define UART0_BASE        0x40005000u
#define UART1_BASE        0x40006000u
#define UART_PINMUX       0x400100F0u

#define TIMER_BASE(n)     (0x4002A000u + (uint32_t)(n) * 0x800u)   /* n = 0..7 */
