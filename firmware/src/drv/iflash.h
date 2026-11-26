#pragma once
#include <stdint.h>

#define IFLASH_WRITE_MIN 0x00108000u   /* update staging area start - nothing below is writable */
#define IFLASH_END       0x00200000u   /* 2 MiB part; OTA record sector is 0x1FF000 */

/* Minimum busy times, independent of the status register (belt and braces after the 99.01 bug):
 * longer than datasheet maxima of the usual 2 MiB parts (4K erase <= 400 ms, page program <= 3 ms).
 * Interrupts are off meanwhile, so the 1 ms tick misses this time (app_port.c adds it back).
 * iflash_calibrate() adds a 25 % margin to the delay loop, so a wait really lasts IFLASH_REAL_MS(ms). */
#define IFLASH_ERASE_MIN_MS 500u
#define IFLASH_PROG_MIN_MS  4u
#define IFLASH_REAL_MS(ms)  ((ms) * 5u / 4u)

int iflash_init(uint32_t clkdiv_reg);              /* flash setup; never writes the status register */
int iflash_write(uint32_t addr, const void *src, uint32_t len);     /* erase 4 KiB sectors + program */
int iflash_program(uint32_t addr, const void *src, uint32_t len);   /* program an already-erased area */

/* delay loop used for the minimum busy times; calibrated against the 1 ms tick (needs tick + GIE on) */
extern uint32_t iflash_loops_per_ms;
void     iflash_delay_loops(uint32_t n);
uint32_t iflash_calibrate(void);
