#pragma once
#include <stdint.h>

void     sys_power_trim_init(void);   /* called from start.S */
void     sys_memctl_reset(void);      /* called from start.S */
void     crt_init(void);              /* called from start.S */

void     sys_wdt_disable(void);
void     sys_memctl_config(void);
void     sys_clock_init(void);        /* 12 MHz xtal -> PLL 480 MHz, APB 60 MHz */
uint32_t sys_apb_hz(void);
uint32_t sys_flash_clkdiv(uint32_t target_hz);
uint32_t sys_timer_hz(void);
void     sys_reset(void) __attribute__((noreturn));
