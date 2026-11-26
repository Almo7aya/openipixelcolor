/* External SPI NOR flash: the 8-pin 25Q32 (4 MiB) next to the main chip, on the SPI master at 0x40003000:
 * SCK/MOSI/MISO on P2.0-P2.2, chip select P2.3. The micro-SD socket shares the bus with its select on P2.7,
 * kept high. Register use and flash commands as the vendor firmware (docs/HARDWARE.md); bytes go one at a
 * time (the vendor firmware moves blocks with DMA channels 6 and 7, not needed here).
 *
 * Unlike the internal flash, this one isn't where the code runs: erasing and programming don't stop the CPU,
 * interrupts stay on. Every wait has a limit, so a missing or stuck chip makes the calls fail, never hang. */
#include "xflash.h"
#include "../hal/regs.h"
#include "../hal/gpio.h"
#include "../hal/timer.h"

#define SPIM        0x40003000u
#define SPI_RX      REG32(SPIM + 0x00u)
#define SPI_TX      REG32(SPIM + 0x04u)
#define SPI_START   REG32(SPIM + 0x08u)
#define SPI_CTRL    REG16(SPIM + 0x0Cu)      /* [3:0] clock divider, [5:4] mode, [10] master, [12] receive,
                                                [13] DMA, [15] enable */
#define SPI_FLAGS   REG8(SPIM + 0x10u)       /* write 1: clear rx, 4: clear done */
#define SPI_STAT    REG32(SPIM + 0x14u)      /* [3] done, [4] busy */
#define PINMUX      REG32(0x400100F0u)       /* bit 3: SPI master on P2.0-P2.2 */

#define CS_PIN      3u                      /* P2.3 */
#define SD_CS_PIN   7u                      /* P2.7 */

#define SPIN_LIMIT  200000u                 /* busy / done polls (a byte takes a few dozen) */

static uint8_t  ok, sr_found;
static uint32_t id;

static void cs(int on) { gpio_write(GPIO_P2, CS_PIN, !on); }

/* one byte each way; rx: clock in a byte (what goes out is don't-care). -1 = the controller didn't finish */
static int xfer(int rx, uint8_t out)
{
    uint16_t c = SPI_CTRL;
    c = (uint16_t)(c & ~0x20C0u);
    c = rx ? (uint16_t)(c | 0x1000u) : (uint16_t)(c & ~0x1000u);
    SPI_CTRL = c;
    SPI_FLAGS = (uint8_t)(SPI_FLAGS | 1u);
    uint32_t n = 0;
    while (SPI_STAT & 0x10u) if (++n > SPIN_LIMIT) return -1;
    if (!rx) SPI_TX = out;
    SPI_START = 1u;
    while (!(SPI_STAT & 0x08u)) if (++n > SPIN_LIMIT) return -1;
    SPI_FLAGS = (uint8_t)(SPI_FLAGS | 4u);
    return rx ? (int)(SPI_RX & 0xFFu) : 0;
}

static int put(uint8_t b) { return xfer(0, b); }
static int get(void)      { return xfer(1, 0); }

static int cmd_addr(uint8_t c, uint32_t a)
{
    return (put(c) | put((uint8_t)(a >> 16)) | put((uint8_t)(a >> 8)) | put((uint8_t)a)) < 0 ? -1 : 0;
}

static int read_sr(uint8_t c)                 /* 0x05 status 1, 0x35 status 2 */
{
    cs(1);
    int r = put(c) < 0 ? -1 : get();
    cs(0);
    return r;
}

static int wren(void)                          /* write enable, checked: WEL must come up */
{
    cs(1); int r = put(0x06); cs(0);
    if (r < 0) return -1;
    int s = read_sr(0x05);
    return s >= 0 && (s & 2) ? 0 : -1;
}

static int wait_ready(uint32_t limit_ms)
{
    uint32_t t0 = millis();
    for (;;) {
        int s = read_sr(0x05);
        if (s < 0) return -1;
        if (!(s & 1)) return 0;
        if (millis() - t0 > limit_ms) return -1;
    }
}

int xflash_init(void)
{
    PINMUX |= 8u;
    gpio_output(GPIO_P2, CS_PIN, 1);
    gpio_output(GPIO_P2, SD_CS_PIN, 1);
    uint16_t c = SPI_CTRL;                    /* as the vendor's spim_config(mode 0, divider 0) */
    c &= 0x7FFFu; c &= (uint16_t)~0xC0u; c |= 0x400u; c &= (uint16_t)~0x7000u; c &= (uint16_t)~0x30u; c &= (uint16_t)~0xFu;
    SPI_CTRL = c;
    SPI_CTRL = (uint16_t)(SPI_CTRL | 0x8000u);
    ok = 0;
    cs(1); int r = put(0xAB); cs(0);          /* wake from deep power-down, if it was */
    if (r < 0) return 0;
    delay_ms(1);
    cs(1);
    int a = put(0x9F) < 0 ? -1 : get(), b = get(), d = get();
    cs(0);
    if (a < 0 || b < 0 || d < 0) return 0;
    id = ((uint32_t)a << 16) | ((uint32_t)b << 8) | (uint32_t)d;
    if (id == 0 || id == 0xFFFFFFu || d < 0x10 || d > 0x20) return 0;
    int s = read_sr(0x05);
    if (s < 0) return 0;
    sr_found = (uint8_t)s;
    if (s & 0x7C) {                           /* block protection set: clear it, or nothing can be written */
        if (wren() != 0) return 0;
        cs(1); r = put(0x01) | put(0x00); cs(0);
        if (r < 0 || wait_ready(100) != 0) return 0;
    }
    ok = 1;
    return 1;
}

int      xflash_ok(void)              { return ok; }
uint32_t xflash_id(void)              { return id; }
uint32_t xflash_size(void)            { return ok ? 1u << (id & 0xFFu) : 0u; }
uint8_t  xflash_status_at_init(void)  { return sr_found; }
uint8_t  xflash_status(void)          { int s = read_sr(0x05); return (uint8_t)(s < 0 ? 0xFF : s); }

int xflash_read(uint32_t addr, void *dst, uint32_t n)
{
    if (!ok || addr + n > xflash_size()) return -1;
    uint8_t *p = dst;
    cs(1);
    int r = cmd_addr(0x0B, addr) | put(0x00);  /* fast read: one dummy byte */
    for (uint32_t i = 0; r >= 0 && i < n; i++) { int v = get(); if (v < 0) r = -1; else p[i] = (uint8_t)v; }
    cs(0);
    return r < 0 ? -1 : 0;
}

int xflash_erase(uint32_t addr)
{
    if (!ok || addr >= xflash_size() || wren() != 0) return -1;
    cs(1); int r = cmd_addr(0x20, addr & ~(XFLASH_SECTOR - 1u)); cs(0);
    return r < 0 ? -1 : wait_ready(1000);      /* typically 50-400 ms */
}

int xflash_program(uint32_t addr, const void *src, uint32_t n)
{
    if (!ok || addr + n > xflash_size()) return -1;
    const uint8_t *p = src;
    while (n) {
        uint32_t k = 256u - (addr & 255u);     /* not across a 256-byte page */
        if (k > n) k = n;
        if (wren() != 0) return -1;
        cs(1);
        int r = cmd_addr(0x02, addr);
        for (uint32_t i = 0; r >= 0 && i < k; i++) r = put(p[i]);
        cs(0);
        if (r < 0 || wait_ready(50) != 0) return -1;
        addr += k; p += k; n -= k;
    }
    return 0;
}

int xflash_write_sector(uint32_t addr, const void *src, uint32_t n)
{
    if (n > XFLASH_SECTOR || (addr & (XFLASH_SECTOR - 1u)) || xflash_erase(addr) != 0) return -1;
    return xflash_program(addr, src, n);
}

/* chip erase: returns at once, the chip works on its own for 10-40 s (xflash_busy) */
int xflash_erase_all_start(void)
{
    if (!ok || wren() != 0) return -1;
    cs(1); int r = put(0xC7); cs(0);
    return r < 0 ? -1 : 0;
}

int xflash_busy(void)
{
    int s = read_sr(0x05);
    return s < 0 ? -1 : (s & 1);
}
