/* openpanel32 firmware (version in app/fwinfo.h) - recoverable build.
 *
 * Recovery design (the device has no programmer access, BLE OTA is the only way in):
 *   1. Clocks/flash/console/tick, then the companion link + OTA receiver come up BEFORE anything risky.
 *   2. The update receiver is byte-compatible with the vendor's: the same tool can install a vendor image to go back.
 *   3. Rescue mode = link + OTA only (no display): hold the key at power-on, or automatic after
 *      3 consecutive resets that happen less than 10 s after boot (crash loop).
 *   4. Exceptions record type/address in .noinit and reset instead of hanging.
 *   5. No unbounded waits in the main loop (UART/flash waits are bounded, display is non-blocking).
 *   6. The flash driver can only write the OTA staging area (0x108000+) - never bootloader or app.
 *   7. Saved settings (app/settings.c) are only read at boot (plain memory reads), applied only on a
 *      clean boot (not after a crash reset), and written from the main loop, never in rescue mode or
 *      during an update.
 *
 * Identify over BLE: cmd 0x8005 -> MCU 99.xx; cmd 0xF0F1 -> mode / crash count / last exception / uptime.
 */
#include <stdint.h>
#include "hal/sys.h"
#include "hal/uart.h"
#include "hal/timer.h"
#include "hal/cpu.h"
#include "hal/gpio.h"
#include "drv/board.h"
#include "drv/display.h"
#include "drv/key.h"
#include "drv/link.h"
#include "drv/iflash.h"
#include "app/ota.h"
#include "app/proto.h"
#include "app/fwinfo.h"
#include "app/modes.h"
#include "app/settings.h"
#include "lib/printf.h"

#define BOOT_MAGIC        0xB007C0DEu
#define CRASH_RESCUE_AT   3u
#define STABLE_MS         10000u

/* survives resets (not cleared by crt_init) */
__attribute__((section(".noinit"))) static volatile uint32_t nv_magic, nv_crash, nv_exc_type, nv_exc_ipc;

volatile uint32_t g_heartbeat;
volatile uint32_t g_boot_stage;
static unsigned g_mode;
static fb_t fb;

unsigned fw_mode(void)          { return g_mode; }
unsigned fw_crash_count(void)   { return nv_crash; }
unsigned fw_last_exc_type(void) { return nv_exc_type; }
uint32_t fw_last_exc_ipc(void)  { return nv_exc_ipc; }
void fw_prepare_reset(void)     { nv_crash = 0; }
void fw_note_exception(unsigned type, uint32_t ipc) { nv_exc_type = type; nv_exc_ipc = ipc; }

void tick_1ms_hook(void) { key_tick_1ms(); }

/* ---- main ---------------------------------------------------------------------------------- */
int main(void)
{
    g_boot_stage = 1;
    sys_wdt_disable();
    sys_memctl_config();            /* also locks the flash-driver window into cache */
    board_gpio_init();
    sys_clock_init();
    int quad = iflash_init(sys_flash_clkdiv(80000000u));
    g_boot_stage = 2;

    uart_pinmux(CONSOLE_UART, 1);
    uart_init(CONSOLE_UART, CONSOLE_BAUD);
    if (nv_magic != BOOT_MAGIC) { nv_magic = BOOT_MAGIC; nv_crash = 0; nv_exc_type = 0; nv_exc_ipc = 0; }
    nv_crash++;
    kprintf("\nopenpanel32 " FW_VERSION_STR " (" __DATE__ " " __TIME__ ") boots=%lu exc=%lu@%08lx quad=%d\n",
            nv_crash, nv_exc_type, nv_exc_ipc, quad);
    g_boot_stage = 3;

    tick_init();
    cpu_gie_enable();
    link_init();                    /* BLE link + OTA available from here on */
    uint32_t lpm = iflash_calibrate();   /* flash-driver minimum-wait timing (50 ms, bounded) */
    kprintf("flash delay loop: %lu loops/ms\n", lpm);
    key_init();
    g_boot_stage = 4;

    if (key_is_down())               g_mode = FW_MODE_RESCUE_KEY;
    else if (nv_crash > CRASH_RESCUE_AT) g_mode = FW_MODE_RESCUE_CRASH;
    else                             g_mode = FW_MODE_NORMAL;

    board_p2_11_set(1);             /* as the vendor firmware after init (may power the BLE chip / panel) */
    if (g_mode == FW_MODE_NORMAL) {
        display_init();
        modes_init(millis());
        /* nv_crash is 1 on a clean boot; >1 means the last run ended in a crash reset, possibly caused by
           a saved setting: then start with factory settings (they replace the saved ones a bit later) */
        settings_load(millis(), nv_crash <= 1u);
    }
    kprintf("mode %u (0=normal 1=rescue-key 2=rescue-crash)\n", g_mode);
    g_boot_stage = 5;

    uint32_t next_sec = millis() + 1000, next_frame = 0, boot_ms = millis();
    int stable = 0;

    for (;;) {
        uint32_t now = millis();

        const uint8_t *pkt;
        unsigned len = link_poll(&pkt);
        if (len) proto_handle(pkt, len);
        proto_tick();

        if (g_mode == FW_MODE_NORMAL && !ota_active()) {
            key_event_t ev = key_poll();
            if (ev == KEY_SHORT) modes_key(MKEY_SHORT, now);
            else if (ev == KEY_LONG) modes_key(MKEY_LONG, now);
            if ((int32_t)(now - next_frame) >= 0) {
                next_frame = now + 25;              /* ~40 fps */
                modes_render(&fb, now);
                display_show(fb);                   /* non-blocking: skipped if previous frame pending */
            }
        }
        if (g_mode == FW_MODE_NORMAL) settings_tick(now, !ota_active());

        if (!stable && (now - boot_ms) >= STABLE_MS) { stable = 1; nv_crash = 0; }

        if ((int32_t)(now - next_sec) >= 0) {
            next_sec += 1000;
            g_heartbeat++;
            kprintf("t=%lus mode=%u rows=%lu ota=%d\n", g_heartbeat, g_mode, display_rows_scanned(), ota_active());
        }
    }
}
