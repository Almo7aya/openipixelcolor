/* Firmware implementation of app_port.h */
#include "app_port.h"
#include "../drv/display.h"
#include "../drv/link.h"
#include "../drv/iflash.h"
#include "../drv/xflash.h"
#include "../hal/timer.h"

void app_set_brightness_pct(unsigned pct)
{
    unsigned level = pct / 6u;                    /* level = min(pct/6, 15), as with the vendor firmware */
    display_set_brightness(level > 15u ? 15u : level);
}

void app_set_orientation(unsigned o) { display_set_orientation(o & 3u); }

void app_reply(uint16_t cmd, uint8_t status, const uint8_t *extra, unsigned n)
{
    if (n > 11u) n = 11u;                         /* the BLE bridge forwards at most 16 bytes */
    link_reply(cmd, status, extra, n);
}

/* XIP: CPU address == flash offset. Callers never read back what they wrote in the same boot (the
 * cache may still hold the old bytes); they keep their own copy in RAM. */
const void *app_flash_ptr(uint32_t addr) { return (const void *)(uintptr_t)addr; }

/* An erase keeps interrupts off for ~0.5 s, so the row scan stops and the row latched at that moment
 * would stay lit at full duty. Blank the picture and let two rows go by first: the panel goes dark
 * briefly instead of flashing one bright line. The main loop draws the next frame afterwards.
 * The 1 ms tick misses that time too; it is added back so the clock does not fall behind. */
int app_flash_write_sector(uint32_t addr, const void *src, uint32_t len)
{
    display_blank();
    uint32_t t0 = millis();
    uint32_t last = display_rows_scanned();
    unsigned seen = 0;
    while (seen < 2u && millis() - t0 < 30u) {
        uint32_t r = display_rows_scanned();
        if (r != last) { last = r; seen++; }
    }
    uint32_t t1 = millis();
    int rc = iflash_write(addr, src, len);
    uint32_t missed = IFLASH_REAL_MS(((len + 0xFFFu) >> 12) * IFLASH_ERASE_MIN_MS + ((len + 0xFFu) >> 8) * IFLASH_PROG_MIN_MS);
    uint32_t seen_ms = millis() - t1;
    if (missed > seen_ms) tick_advance(missed - seen_ms);
    return rc;
}

int app_flash_program(uint32_t addr, const void *src, uint32_t len) { return iflash_program(addr, src, len); }

/* The controller caches flash reads and a write does not update that cache: bytes read before a write can
 * come back stale afterwards (seen on the panel when an animation that had been playing was overwritten).
 * Reading 256 KiB of other flash pushes every cached line out (the cache is far smaller). The locked
 * flash-driver window is not affected, and nothing here writes. Takes a few ms. */
void app_flash_forget(void)
{
    const volatile uint32_t *p = (const volatile uint32_t *)0x00011000u;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < 0x40000u / 4u; i++) sum += p[i];
    (void)sum;
}

/* the external flash (drv/xflash.c), found the first time it's asked about. Writing it doesn't stop the CPU
 * (the code runs from the internal flash), so there's nothing to blank or catch up. */
uint32_t app_xflash_size(void)
{
    static uint8_t tried;
    if (!tried) { tried = 1; xflash_init(); }
    return xflash_size();
}
int app_xflash_read(uint32_t addr, void *dst, uint32_t len)                { return xflash_read(addr, dst, len); }
int app_xflash_program(uint32_t addr, const void *src, uint32_t len)       { return xflash_program(addr, src, len); }
int app_xflash_write_sector(uint32_t addr, const void *src, uint32_t len)  { return xflash_write_sector(addr, src, len); }
int app_xflash_erase_all(void)                                             { return xflash_erase_all_start(); }
int app_xflash_busy(void)                                                  { return xflash_busy(); }
