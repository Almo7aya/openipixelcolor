/* Firmware implementation of app_port.h */
#include "app_port.h"
#include "../drv/display.h"
#include "../drv/link.h"
#include "../drv/iflash.h"
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
 * briefly instead of flashing one bright line. The main loop draws the next frame afterwards. */
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
    return iflash_write(addr, src, len);
}

int app_flash_program(uint32_t addr, const void *src, uint32_t len) { return iflash_program(addr, src, len); }
