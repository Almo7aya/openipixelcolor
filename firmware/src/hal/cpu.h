/* CPU helpers implemented in start.S */
#pragma once
#include <stdint.h>

void     cpu_gie_enable(void);
void     cpu_gie_disable(void);
uint32_t cpu_int_mask2_get(void);
void     cpu_int_mask2_set(uint32_t v);
void     cpu_mmu_ctl_or(uint32_t bits);
uint32_t cpu_psw_get(void);

static inline uint32_t cpu_irq_save(void)
{
    uint32_t was_on = cpu_psw_get() & 1u;
    cpu_gie_disable();
    return was_on;
}

static inline void cpu_irq_restore(uint32_t was_on)
{
    if (was_on) cpu_gie_enable();
}
