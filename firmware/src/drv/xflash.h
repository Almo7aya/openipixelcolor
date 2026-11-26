/* External SPI NOR flash: the 8-pin 25Q32 (4 MiB) next to the main chip (docs/HARDWARE.md). */
#pragma once
#include <stdint.h>

#define XFLASH_SECTOR 4096u

int      xflash_init(void);                                   /* 1: a chip answered and can be written */
int      xflash_ok(void);                                     /* xflash_init() found it */
uint32_t xflash_id(void);                                     /* JEDEC id: maker << 16 | type << 8 | capacity */
uint32_t xflash_size(void);                                   /* bytes, from the id (0 = none) */
uint8_t  xflash_status(void);                                 /* status register 1 */
uint8_t  xflash_status_at_init(void);                         /* status register 1 as found (before unprotecting) */

int      xflash_read(uint32_t addr, void *dst, uint32_t n);    /* 0 ok, -1 fail */
int      xflash_erase(uint32_t addr);                          /* the 4 KiB sector holding addr */
int      xflash_program(uint32_t addr, const void *src, uint32_t n);   /* erased bytes only; any length */
int      xflash_write_sector(uint32_t addr, const void *src, uint32_t n);   /* erase, then program n <= 4 KiB */
int      xflash_erase_all_start(void);                         /* the whole chip; xflash_busy() until done */
int      xflash_busy(void);                                    /* 1 busy, 0 ready, -1 no answer */
