/* System bring-up: the same init sequences the vendor firmware uses. */
#include "sys.h"
#include "regs.h"
#include "cpu.h"

static uint32_t g_xtal_hz;
static uint32_t g_pll_hz;

/* analog power trims - called from start.S before the system registers are set */
void sys_power_trim_init(void)
{
    REG32(ANA_E810) |= (REG32(ANA_E810) >> 4) & 0x3Cu;
    REG32(ANA_APPLY) = 0;
    REG32(ANA_E804) |= 0x08u;
    REG32(ANA_APPLY) = 0;
    REG32(ANA_E804) |= 0x20u;
    REG32(ANA_APPLY) = 0;
    REG32(ANA_E804) |= 0x04u;
    REG32(ANA_APPLY) = 0;
}

/* memory controller back to its reset state - called after the system registers */
void sys_memctl_reset(void)
{
    REG32(MEMCTL_BASE + 0x00) &= 0xFFFEu;          /* note: also clears bits 31..16, as the vendor firmware does */
    REG32(MEMCTL_BASE + 0x08) &= ~1u;
    REG32(MEMCTL_BASE + 0x04) &= ~1u;
    REG32(MEMCTL_CFG) = 0x55;
    REG32(MEMCTL_BASE + 0x38) = 0;
}

/* probably the watchdog off; the vendor firmware never feeds a watchdog either */
void sys_wdt_disable(void)
{
    uint32_t s = cpu_irq_save();
    REG32(PMU_WDT_A) = 1;
    REG32(PMU_WDT_B) = 1;
    cpu_irq_restore(s);
}

/* cache + memory map: 4 KiB at 0x10000 locked into cache (the flash driver runs from there) */
void sys_memctl_config(void)
{
    cpu_mmu_ctl_or(6);
    REG32(MEMCTL_BASE + 0x00) &= ~1u;
    REG32(MEMCTL_BASE + 0x08) &= ~1u;
    REG32(MEMCTL_BASE + 0x04) &= ~1u;
    REG32(MEMCTL_CFG) = 0x55;
    REG32(MEMCTL_BASE + 0x38) = 0;

    REG32(MEMCTL_CFG) = (REG32(MEMCTL_CFG) & 0xFFFFFF00u) | 0x57u;

    REG32(MEMCTL_BASE + 0x00) &= 0xFFFFFF83u;      /* region 0 */
    for (uint32_t n = 0; n < 1000000u && (REG32(MEMCTL_BASE + 0x00) & 0x80u); n++) { }
    REG32(MEMCTL_BASE + 0x00) |= 3u;

    REG32(MEMCTL_BASE + 0x04) = (REG32(MEMCTL_BASE + 0x04) & 0xFF000001u) | (0x40u << 10) | (4u << 1);
    REG32(MEMCTL_BASE + 0x08) = (REG32(MEMCTL_BASE + 0x08) & 0xFF000001u);

    REG32(MEMCTL_BASE + 0x00) = (REG32(MEMCTL_BASE + 0x00) & 0xFFFFFF83u) | 6u;   /* window 0x10000, 4 KiB, preload */
    for (uint32_t n = 0; n < 1000000u && (REG32(MEMCTL_BASE + 0x00) & 0x80u); n++) { }
    REG32(MEMCTL_BASE + 0x04) |= 1u;
    REG32(MEMCTL_BASE + 0x38) = 0;
}

/* clock tree: 12 MHz crystal, PLL 480 MHz, peripheral clocks on */
void sys_clock_init(void)
{
    const uint32_t xtal = 12000000u;
    const uint32_t pll  = 480000000u;

    /* crystal */
    REG32(ANA_XTAL) = 0x13;
    REG32(PMU_XTAL_CFG) = (REG32(PMU_XTAL_CFG) & 0xFFEFu) | 0x20u;
    REG32(ANA_E808) &= ~4u;
    REG32(PLL_REFDIV) = xtal / 31250u - 1u;        /* 383 -> 31.25 kHz reference */
    REG32(CLK_SEL) &= 0xFFF7u;
    REG32(CLK_DIV_32K) = (xtal >> 15) - 1u;        /* 365 */
    REG32(ANA_APPLY) = 0;
    g_xtal_hz = xtal;

    /* PLL */
    for (uint32_t n = 0; n < 5000000u && REG32(PMU_XTAL_READY) == 0; n++) { }
    REG32(PLL_CFG44) = 0;
    REG32(PLL_FBDIV) = (pll / (xtal / (REG32(PLL_REFDIV) + 1u))) << 8;   /* 15360 << 8 */
    REG32(PLL_CFG1C) = 0x40B;
    REG32(PLL_CFG0C) = 0x200;
    REG32(PLL_CTRL) = (REG32(PLL_CTRL) & 0xFFFEu) | 7u;
    for (uint32_t n = 0; n < 5000000u && (REG32(PLL_STATUS) & 1u) == 0; n++) { }
    g_pll_hz = pll;

    REG32(CLK_SEL) |= 1u;          /* system clock <- PLL */
    REG32(CLK_GATE0) = 0x7FFFFu;   /* all peripheral clocks on */
    REG32(CLK_GATE1) = 0xFFu;
    REG32(CLK_SEL) |= 0x20u;       /* APB = PLL / 8 = 60 MHz */
}

/* value for 0x40021040: PLL/target in 8.8 format,
 * fraction 0 replaced by 5 (as the vendor firmware does). 480 MHz / 80 MHz -> 0x605. */
uint32_t sys_flash_clkdiv(uint32_t target_hz)
{
    if (target_hz - 1u > 95999999u) target_hz = 96000000u;
    uint32_t t = target_hz / 1000u, p = g_pll_hz / 1000u;
    uint32_t frac = ((p % t) << 8) / t;
    if (frac == 0) frac = 5;
    return frac | ((p / t) << 8);
}

uint32_t sys_apb_hz(void)
{
    return (REG32(CLK_SEL) & 0x20u) ? (g_pll_hz >> 3) : g_xtal_hz;
}

uint32_t sys_timer_hz(void)
{
    uint32_t c = (REG32(CLK_SEL) & 1u) ? (g_pll_hz >> 2) : g_xtal_hz;
    uint32_t d = (REG32(CLK_SEL) & 4u) ? REG32(CLK_DIV_TIMER) + 1u : 1u;
    return c / d;
}

void sys_reset(void)
{
    REG32(SYS_RESET) = 1;
    for (;;) { }
}

/* C runtime: copy .data, clear .bss (called from start.S before main) */
extern uint32_t __data_load[], __data_start[], __data_end[], __bss_start[], __bss_end[];
void crt_init(void)
{
    uint32_t *src = __data_load, *dst = __data_start;
    while (dst < __data_end) *dst++ = *src++;
    for (dst = __bss_start; dst < __bss_end; ) *dst++ = 0;
}
