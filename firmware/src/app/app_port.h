/* What the app layer needs from the platform (firmware: app_port.c, PC simulator: stubs). */
#pragma once
#include <stdint.h>

void app_set_brightness_pct(unsigned pct);      /* 0..100 */
void app_set_orientation(unsigned o);           /* 0..3, same meaning as in the vendor app */
void app_reply(uint16_t cmd, uint8_t status, const uint8_t *extra, unsigned n);   /* <= 11 extra bytes */

/* Storage for settings.c (firmware: internal flash; simulator: a RAM array with NOR-flash rules).
 * Reads are plain memory reads. Return 0 = ok, <0 = error. */
const void *app_flash_ptr(uint32_t addr);
int app_flash_write_sector(uint32_t addr, const void *src, uint32_t len);   /* erase 4 KiB, then program */
int app_flash_program(uint32_t addr, const void *src, uint32_t len);        /* program erased bytes only */
void app_flash_forget(void);    /* drop cached flash reads: call before reading back what was rewritten */
