/* The panel in the browser: firmware/src/app (modes, effects, clocks, text effects, animation player)
 * compiled unchanged to WebAssembly by tools/wasm/build.py -> webui/panel.wasm. The web UI feeds it every
 * packet it sends to the real panel and keeps it in step with the panel's status, so the preview draws
 * what the panel draws (webui/js/virtual.js). No C library: the firmware code doesn't use one.
 *
 * Exports: vp_init(now)        start (as after switching on, nothing saved)
 *          vp_in()             address of the packet buffer (VP_IN_MAX bytes)
 *          vp_cmd(len, now)    handle the packet in the buffer; the reply (16 bytes) is at vp_reply()
 *          vp_render(now)      draw a frame; returns the address of 32*32*3 RGB bytes
 *          vp_bright()         brightness the panel would use, 0..100 */
#include <stdint.h>
#include "gfx.h"
#include "modes.h"
#include "app_port.h"
#include "anim.h"

#define EXPORT(name) __attribute__((export_name(#name)))
#define VP_IN_MAX 8192u

/* the compiler may call these for struct copies and clears */
void *memset(void *d, int c, unsigned long n) { uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned long n) { uint8_t *p = d; const uint8_t *q = s; while (n--) *p++ = *q++; return d; }
void *memmove(void *d, const void *s, unsigned long n)
{
    uint8_t *p = d; const uint8_t *q = s;
    if (p < q) while (n--) *p++ = *q++;
    else { p += n; q += n; while (n--) *--p = *--q; }
    return d;
}
int memcmp(const void *a, const void *b, unsigned long n)
{
    const uint8_t *p = a, *q = b;
    for (; n; n--, p++, q++) if (*p != *q) return *p - *q;
    return 0;
}

/* ---- platform ---- */
static uint8_t reply[16];
static unsigned bright = 100;

void app_set_brightness_pct(unsigned p) { bright = p; }
void app_set_orientation(unsigned o)    { (void)o; }     /* the preview stays upright */
void app_reply(uint16_t c, uint8_t s, const uint8_t *x, unsigned n)
{
    for (unsigned i = 0; i < sizeof reply; i++) reply[i] = 0;
    reply[0] = 16; reply[2] = (uint8_t)c; reply[3] = (uint8_t)(c >> 8); reply[4] = s;
    for (unsigned i = 0; i < n && i < 11u; i++) reply[5 + i] = x[i];
}

/* flash from the animation store up (animations, picture, text, settings): RAM with NOR rules */
#define FL_BASE ANIM_BASE
static uint8_t flash_mem[0x200000u - ANIM_BASE];

const void *app_flash_ptr(uint32_t a) { return flash_mem + (a - FL_BASE); }
int app_flash_program(uint32_t a, const void *src, uint32_t n)
{
    if (a < FL_BASE || a + n > FL_BASE + sizeof flash_mem) return -1;
    const uint8_t *b = src;
    for (uint32_t i = 0; i < n; i++) flash_mem[a - FL_BASE + i] &= b[i];
    return 0;
}
int app_flash_write_sector(uint32_t a, const void *src, uint32_t n)
{
    if ((a & 0xFFFu) || n > 4096u || a < FL_BASE || a + 4096u > FL_BASE + sizeof flash_mem) return -1;
    for (uint32_t i = 0; i < 4096u; i++) flash_mem[a - FL_BASE + i] = 0xFF;
    return app_flash_program(a, src, n);
}
void app_flash_forget(void) { }

/* the external flash: 4 MiB of RAM, erased in no time (anim.c then sets it up at once) */
static uint8_t xmem[4u << 20];
uint32_t app_xflash_size(void) { return sizeof xmem; }
int app_xflash_busy(void)      { return 0; }
int app_xflash_read(uint32_t a, void *d, uint32_t n)
{
    if (a + n > sizeof xmem) return -1;
    uint8_t *p = d;
    for (uint32_t i = 0; i < n; i++) p[i] = xmem[a + i];
    return 0;
}
int app_xflash_program(uint32_t a, const void *src, uint32_t n)
{
    if (a + n > sizeof xmem) return -1;
    const uint8_t *b = src;
    for (uint32_t i = 0; i < n; i++) xmem[a + i] &= b[i];
    return 0;
}
int app_xflash_write_sector(uint32_t a, const void *src, uint32_t n)
{
    if ((a & 0xFFFu) || n > 4096u || a + 4096u > sizeof xmem) return -1;
    for (uint32_t i = 0; i < 4096u; i++) xmem[a + i] = 0xFF;
    return app_xflash_program(a, src, n);
}
int app_xflash_erase_all(void) { for (uint32_t i = 0; i < sizeof xmem; i++) xmem[i] = 0xFF; return 0; }

/* ---- exports ---- */
static fb_t fb;
static uint8_t in[VP_IN_MAX] __attribute__((aligned(4)));

EXPORT(vp_init) void vp_init(uint32_t now)
{
    for (uint32_t i = 0; i < sizeof flash_mem; i++) flash_mem[i] = 0xFF;
    bright = 100;
    modes_init(now);
}
EXPORT(vp_in) uint8_t *vp_in(void) { return in; }
EXPORT(vp_reply) uint8_t *vp_reply(void) { return reply; }
EXPORT(vp_cmd) int vp_cmd(unsigned len, uint32_t now)
{
    if (len < 4 || len > VP_IN_MAX) return 0;
    for (unsigned i = 0; i < sizeof reply; i++) reply[i] = 0;
    uint16_t cmd = (uint16_t)(in[2] | (in[3] << 8));
    if (cmd == 0x8001) {                                  /* time: firmware/src/app/proto.c */
        if (len >= 7) modes_set_time(in[4], in[5], in[6], now);
        return 1;
    }
    return modes_handle_cmd(cmd, in, len, now);
}
EXPORT(vp_render) uint8_t *vp_render(uint32_t now)
{
    modes_render(&fb, now);
    return &fb[0][0][0];
}
EXPORT(vp_bright) unsigned vp_bright(void) { return bright; }
