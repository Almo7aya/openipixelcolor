#pragma once
#include <stdint.h>

#define IFLASH_WRITE_MIN 0x00108000u   /* update staging area start - nothing below is writable */
#define IFLASH_END       0x00200000u   /* 2 MiB part; OTA record sector is 0x1FF000 */

int iflash_init(uint32_t clkdiv_reg);              /* flash setup; never writes the status register */
int iflash_write(uint32_t addr, const void *src, uint32_t len);     /* erase 4 KiB sectors + program */
int iflash_program(uint32_t addr, const void *src, uint32_t len);   /* program an already-erased area */

/* delay loop used for the minimum busy times; calibrated against the 1 ms tick (needs tick + GIE on) */
extern uint32_t iflash_loops_per_ms;
void     iflash_delay_loops(uint32_t n);
uint32_t iflash_calibrate(void);
