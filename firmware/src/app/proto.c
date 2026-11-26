/* Companion-link command handler. Reply formats match the vendor firmware, so existing apps keep working (docs/PROTOCOL.md).
 * Only what is needed for identification, firmware update (incl. going back to the vendor firmware) and diagnostics. */
#include "proto.h"
#include "ota.h"
#include "fwinfo.h"
#include "../drv/link.h"
#include "../drv/iflash.h"
#include "../drv/display.h"
#include "../drv/xflash.h"
#include "modes.h"
#include "settings.h"
#include "anim.h"
#include "app_port.h"
#include "../hal/sys.h"
#include "../hal/timer.h"
#include "../lib/printf.h"

#include "board.h"

static uint8_t  bt_ver_major, bt_ver_minor;
static uint32_t ota_deadline;          /* 0 = disarmed; 10 s inactivity reset after update commands, as the vendor firmware */
static uint32_t n_packets, n_unknown;

/* ---- platform hooks for ota.c ------------------------------------------------------------- */
/* Normal mode: the display keeps running during an update (main.c shows the update screen), so each block
 * is written the way saved settings and animations are: picture blanked first, clock kept (app_port.c).
 * Rescue mode: no display, plain writes - the recovery path stays exactly as it was. */
int ota_port_flash_write(uint32_t addr, const void *buf, uint32_t len)
{
    return fw_mode() == FW_MODE_NORMAL ? app_flash_write_sector(addr, buf, len) : iflash_write(addr, buf, len);
}
const void *ota_port_flash_ptr(uint32_t addr) { return (const void *)(uintptr_t)addr; }   /* XIP: CPU addr == flash offset */
void ota_port_final_ack(void) { link_reply(0xC001, OTA_OK, 0, 0); }
void ota_port_reset(void)     { fw_prepare_reset(); sys_reset(); }

static void arm_ota_timer(void) { ota_deadline = millis() + 10000u; if (!ota_deadline) ota_deadline = 1; }

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* 0xF114 external flash, read only (docs/PROTOCOL.md). sub 0: id(3) status-as-found status-now ok
 * kib_read(2) ms(2) (a read-speed test: 16 KiB); sub 2 addr(3): the 10 bytes there. */
static unsigned xflash_probe(unsigned sub, const uint8_t *p, unsigned len, uint8_t *x)
{
    uint8_t buf[256];
    if (!app_xflash_size()) return 0;
    if (sub == 0) {
        uint32_t i = xflash_id(), t0 = millis(), kib = 0;
        x[0] = (uint8_t)(i >> 16); x[1] = (uint8_t)(i >> 8); x[2] = (uint8_t)i;
        x[3] = xflash_status_at_init(); x[4] = xflash_status(); x[5] = (uint8_t)xflash_ok();
        for (uint32_t a = 0; xflash_ok() && a < 16384u; a += sizeof buf, kib++) if (xflash_read(a, buf, sizeof buf) != 0) break;
        uint32_t ms = millis() - t0;
        kib /= 4u;
        x[6] = (uint8_t)kib; x[7] = (uint8_t)(kib >> 8); x[8] = (uint8_t)ms; x[9] = (uint8_t)(ms >> 8);
        return xflash_ok();
    }
    if (sub == 2) {
        uint32_t a = len >= 8 ? ((uint32_t)p[5] << 16) | ((uint32_t)p[6] << 8) | p[7] : 0;
        x[0] = (uint8_t)(a >> 16);
        return xflash_read(a, x + 1, 10) == 0;
    }
    return 0;
}

/* The companion module doesn't say when an app connects, but every app sends 0x8001 (time sync) first, and
 * a connected app keeps talking (the web UI polls every 4 s). So 0x8001 after 8 s of silence = connected. */
#define QUIET_MS 8000u
static uint32_t last_rx;

void proto_handle(const uint8_t *pkt, unsigned len)
{
    uint16_t cmd = link_cmd(pkt);
    uint32_t now = millis();
    int fresh = n_packets == 0 || now - last_rx > QUIET_MS;
    last_rx = now;
    n_packets++;

    switch (cmd) {
    case 0x8001: {                                     /* device info + time sync (every app sends it on connect) */
        if (len >= 7) modes_set_time(pkt[4], pkt[5], pkt[6], now);
        if (fresh && fw_mode() == FW_MODE_NORMAL) modes_connected(now);
        uint8_t x[5] = { 0, 0, 0, 0, 1 };              /* vendor reply: 0a 00 01 80 <type> <u32 setting> 01 */
        link_reply(0x8001, BOARD_DEVICE_TYPE, x, sizeof x);
        break; }
    case 0x8005: {                                     /* versions: 08 00 05 80 mcu_maj mcu_min bt_maj bt_min */
        uint8_t x[3] = { (uint8_t)FW_VER_MINOR, bt_ver_major, bt_ver_minor };   /* the version number: fwinfo.h */
        link_reply(0x8005, (uint8_t)FW_VER_MAJOR, x, 3);
        break; }
    case 0xC000: {                                     /* OTA begin */
        anim_abort_upload();                           /* one flash writer at a time */
        int ok = (len >= 13) ? ota_begin(pkt[4], rd32(pkt + 5), rd32(pkt + 9)) : (int)OTA_FAIL;
        kprintf("ota begin: count %u size %lu -> %d\n", pkt[4], len >= 13 ? rd32(pkt + 9) : 0, ok);
        arm_ota_timer();                               /* (normal mode: main.c now shows the update screen) */
        link_reply(0xC000, (uint8_t)ok, 0, 0);
        break; }
    case 0xC001: {                                     /* OTA chunk (last one resets the chip) */
        int ok = ota_chunk(pkt, len);
        arm_ota_timer();
        link_reply(0xC001, (uint8_t)ok, 0, 0);
        if (!ok) kprintf("ota chunk %u failed\n", len > 4 ? pkt[4] : 0);
        break; }
    case 0xC002: case 0xC003:                          /* companion-module OTA mode: just acknowledge */
        arm_ota_timer();
        link_reply(cmd, 1, 0, 0);
        break;
    case 0xC004:                                       /* reply as 0xC003, then reset (vendor behaviour) */
        link_reply(0xC003, 1, 0, 0);
        ota_port_reset();
        break;
    case 0xC005:                                       /* module reports its version, no reply */
        if (len >= 6) { bt_ver_major = pkt[4]; bt_ver_minor = pkt[5]; }
        break;
    case 0xC006:                                       /* reset */
        link_reply(0xC006, 1, 0, 0);
        ota_port_reset();
        break;
    case 0xF000:                                       /* ping */
        link_reply(0xF000, 1, 0, 0);
        break;
    case FW_CMD_DIAG: {                                /* openipixelcolor: 16-byte status (BLE bridge forwards <=16 B) */
        uint8_t x[11];
        x[0] = (uint8_t)fw_mode();
        x[1] = (uint8_t)fw_crash_count();
        x[2] = (uint8_t)fw_last_exc_type();
        wr32(x + 3, fw_last_exc_ipc());
        wr32(x + 7, millis() / 1000u);
        link_reply(FW_CMD_DIAG, 1, x, sizeof x);
        break; }
    case FW_CMD_PANEL_INFO: {                          /* openipixelcolor: the matrix size and board (board.h) */
        uint8_t x[6] = { (uint8_t)BOARD_W, (uint8_t)(BOARD_W >> 8), (uint8_t)BOARD_H, (uint8_t)(BOARD_H >> 8),
                         (uint8_t)BOARD_ID, (uint8_t)BOARD_DEVICE_TYPE };
        link_reply(FW_CMD_PANEL_INFO, 1, x, sizeof x);
        break; }
    case FW_CMD_XFLASH: {                              /* openipixelcolor: external flash probe */
        uint8_t x[11] = { 0 };
        unsigned r = fw_mode() == FW_MODE_NORMAL && !ota_active() ? xflash_probe(len >= 5 ? pkt[4] : 0, pkt, len, x) : 0;
        link_reply(FW_CMD_XFLASH, (uint8_t)r, x, sizeof x);
        break; }
    case 0xF109: {                                     /* openipixelcolor: saved-settings status (see settings.c) */
        uint8_t x[8];
        link_reply(0xF109, 1, x, settings_info(x));
        break; }
    default: {
        int r = ota_active() ? -1 : modes_handle_cmd(cmd, pkt, len, millis());
        if (r == 0 || r == 1) link_reply(cmd, (uint8_t)r, 0, 0);
        if (r < 0) {
            n_unknown++;
            kprintf("link: unhandled cmd %04x len %u\n", cmd, len);
        }
        break; }
    }
}

void proto_tick(void)
{
    if (ota_deadline && (int32_t)(millis() - ota_deadline) >= 0) {
        kprintf("ota: 10 s inactivity -> reset\n");
        ota_port_reset();
    }
}
