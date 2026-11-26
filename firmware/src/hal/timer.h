#pragma once
#include <stdint.h>

void     timer_init_periodic(unsigned n, uint32_t period_us, unsigned one_shot);
void     timer_start(unsigned n);
void     timer_ack(unsigned n);

void     tick_init(void);             /* 1 ms tick: timer 1, IRQ 8 */
void     tick_1ms_hook(void);         /* weak; define to run code in the tick ISR */
uint32_t millis(void);
void     tick_advance(uint32_t ms);   /* add time the tick missed (long interrupts-off sections) */
void     delay_ms(uint32_t ms);
