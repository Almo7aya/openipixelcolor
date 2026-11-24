/* Interrupt table used by _irq_common in start.S. */
#include "irq.h"
#include "cpu.h"
#include "../lib/printf.h"

static void isr_unhandled(unsigned irq)
{
    /* mask it so we don't livelock, and report once */
    irq_disable(irq);
    kprintf("[irq] unhandled IRQ %u - masked\n", irq);
}

isr_t isr_table[32] = {
    [0 ... 31] = isr_unhandled,
};

void irq_register(unsigned n, isr_t f)
{
    if (n < 32u) isr_table[n] = f ? f : isr_unhandled;
}

void irq_enable(unsigned n)
{
    if (n >= 32u) return;
    uint32_t s = cpu_irq_save();
    cpu_int_mask2_set(cpu_int_mask2_get() | (1u << n));
    cpu_irq_restore(s);
}

void irq_disable(unsigned n)
{
    if (n >= 32u) return;
    uint32_t s = cpu_irq_save();
    cpu_int_mask2_set(cpu_int_mask2_get() & ~(1u << n));
    cpu_irq_restore(s);
}

static const char *const exc_names[] = {
    "?", "TLB fill", "PTE not present", "TLB misc", "TLB VLPT miss",
    "machine error", "debug", "general exception", "syscall",
};

/* Crash: remember what happened (survives the reset in .noinit), report, then reset.
 * main() counts consecutive early resets and falls back to rescue mode (link + OTA only). */
void fw_note_exception(unsigned type, uint32_t ipc);
void sys_reset(void) __attribute__((noreturn));

void c_exception(unsigned type, uint32_t ipc, uint32_t itype, uint32_t eva, uint32_t sp)
{
    fw_note_exception(type, ipc);
    kprintf("\n*** exception %u (%s) ipc=%08lx itype=%08lx eva=%08lx sp=%08lx -> reset\n",
            type, type < 9u ? exc_names[type] : "?", ipc, itype, eva, sp);
    for (volatile uint32_t n = 0; n < 3000000u; n++) { }
    sys_reset();
}
