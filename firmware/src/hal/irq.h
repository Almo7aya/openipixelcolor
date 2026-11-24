#pragma once
#include <stdint.h>

typedef void (*isr_t)(unsigned irq);

extern isr_t isr_table[32];

void irq_register(unsigned n, isr_t f);
void irq_enable(unsigned n);
void irq_disable(unsigned n);
void c_exception(unsigned type, uint32_t ipc, uint32_t itype, uint32_t eva, uint32_t sp);
