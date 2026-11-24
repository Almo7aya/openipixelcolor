/* Timers 0..7 at 0x4002A000 + n*0x800. */
#include "timer.h"
#include "regs.h"
#include "sys.h"
#include "irq.h"

#define T_CTRL(n)   REG32(TIMER_BASE(n) + 0x00)
#define T_PERIOD(n) REG32(TIMER_BASE(n) + 0x0C)
#define T_PRESC(n)  REG32(TIMER_BASE(n) + 0x10)

void timer_init_periodic(unsigned n, uint32_t period_us, unsigned one_shot)
{
    uint32_t clk = sys_timer_hz();
    if (period_us > 0x16C13E9u) period_us = 0x16C13E9u;

    /* extra prescaler shift when the period needs more than 16 bits */
    unsigned lz = 0;
    for (uint32_t v = period_us; (v & 0x80000000u) == 0; v = (v << 1) | 1u) lz++;
    unsigned bits = 32u - lz;
    unsigned shift = (bits <= 16u) ? 0u : (bits - 16u);

    T_CTRL(n) &= ~1u;
    T_CTRL(n) = (T_CTRL(n) & ~2u) | ((one_shot & 1u) << 1);
    T_CTRL(n) |= 8u;
    T_CTRL(n) &= ~0x10u;
    T_CTRL(n) &= ~0x60u;
    T_CTRL(n) |= 0x1000u;
    T_CTRL(n) |= 0x2000u;
    T_CTRL(n) &= ~0x100u;
    T_PERIOD(n) = (period_us >> shift) - 1u;
    T_PRESC(n)  = ((clk / 1000000u) << shift) - 1u;        /* 1 us ticks */
    REG16(TIMER_BASE(n) + 0x22) &= 0xE1FFu;
    REG16(TIMER_BASE(n) + 0x20) &= 0xE1FFu;
    REG16(TIMER_BASE(n) + 0x1E) &= 0xE1FFu;
    REG16(TIMER_BASE(n) + 0x1C) &= 0xE1FFu;
    T_CTRL(n) |= 4u;
    T_CTRL(n) &= ~1u;
    T_CTRL(n) |= 0x1000000u;
}

void timer_start(unsigned n)
{
    T_CTRL(n) |= 1u;
    T_CTRL(n) |= 0x1000000u;
}

void timer_ack(unsigned n)
{
    T_CTRL(n) |= 0x2000u;          /* clear the update flag */
}

/* --- 1 ms system tick on timer 1 / IRQ 8 (as the vendor firmware) -------------------------------- */
#define TICK_TIMER 1u
#define TICK_IRQ   8u

static volatile uint32_t g_ms;

/* override in application code to run work every millisecond (interrupt context) */
__attribute__((weak)) void tick_1ms_hook(void) { }

static void tick_isr(unsigned irq)
{
    (void)irq;
    timer_ack(TICK_TIMER);
    g_ms++;
    tick_1ms_hook();
}

void tick_init(void)
{
    irq_register(TICK_IRQ, tick_isr);
    timer_init_periodic(TICK_TIMER, 1000u, 0);
    irq_enable(TICK_IRQ);
    timer_start(TICK_TIMER);
}

uint32_t millis(void) { return g_ms; }

void delay_ms(uint32_t ms)
{
    uint32_t t0 = g_ms;
    while ((uint32_t)(g_ms - t0) < ms) { }
}
