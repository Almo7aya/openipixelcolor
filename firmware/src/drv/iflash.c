/* Internal (boot/XIP) SPI-NOR driver - controller 0x40011000 (docs/HARDWARE.md).
 *
 * Code executes from this same flash, so every function here lives in
 * .text.flashdrv, which the linker places inside 0x10000-0x10FFF. sys_memctl_config() (same setup as the
 * vendor firmware) locks and preloads that 4 KiB window into cache, so these routines keep running while the
 * flash is busy. Interrupts are off for every operation (ISRs live outside the window).
 * Rules for this file: no calls outside the window (only cpu_* helpers from start.S, also in the window),
 * no const tables, no division. Compiled with -fno-jump-tables.
 *
 * Safety: writes are only allowed in [IFLASH_WRITE_MIN, IFLASH_END) = OTA staging area + OTA record
 * (the saved settings live at the top of the staging area, see app/settings.c).
 * The bootloader (0x0-0xFFFF) and the running application (0x10000-0x107FFF) can never be touched.
 */
#include "iflash.h"
#include "../hal/regs.h"
#include "../hal/cpu.h"

#define FDRV       __attribute__((section(".text.flashdrv"), noinline))
#define FMC(o)     REG32(0x40011000u + (o))
#define SPIN_MAX   0x00800000u

static FDRV int spin_while_set(uint32_t off, uint32_t mask)
{
    for (uint32_t n = 0; n < SPIN_MAX; n++)
        if ((FMC(off) & mask) == 0) return 0;
    return -1;
}

static FDRV void fmc_cmd(uint32_t op, uint32_t cmdbits, uint32_t addrbits, uint32_t flags, uint32_t len1)
{
    FMC(0x10) = op;
    FMC(0x18) = addrbits | (cmdbits << 6);
    FMC(0x1C) = flags;
    FMC(0x0C) = len1;
    FMC(0x20) = 1;
    spin_while_set(0x2C, 1);
}

static FDRV void fmc_exit_crm(void)            /* incl. the two dummy reads */
{
    fmc_cmd(0xFF, 7, 0, 0x700, 0);
    FMC(0x94) = 1;
    (void)FMC(0x98);
    (void)FMC(0x98);
    for (uint32_t n = 0; n < SPIN_MAX && FMC(0x98) != 0; n++) { }
}

/* status: SR1 | SR2 << 8.
 * ORDER MATTERS: wait for the
 * transfer (+0x24 bit0) to finish BEFORE reading the RX register. Reading first returns the previous
 * byte, the busy bit looks clear during an erase, the driver returns while the flash is busy and the CPU
 * then fetches code from busy flash -> hang (bug in 99.01). */
static FDRV uint32_t flash_status(void)
{
    if (FMC(0x50) & 1u) fmc_exit_crm();
    FMC(0x28) = 1;
    fmc_cmd(0x05, 7, 0, 0x600, 0);
    spin_while_set(0x24, 1);
    uint32_t sr1 = FMC(0x00) & 0xFFu;
    fmc_cmd(0x35, 7, 0, 0x600, 0);
    spin_while_set(0x24, 1);
    uint32_t sr2 = FMC(0x00) & 0xFFu;
    FMC(0x28) = 0;
    return (sr2 << 8) | sr1;
}

/* minimum busy times: see iflash.h */
#define ERASE_MIN_MS IFLASH_ERASE_MIN_MS
#define PROG_MIN_MS  IFLASH_PROG_MIN_MS

uint32_t iflash_loops_per_ms;                      /* set by iflash_calibrate(); writes refused while 0 */

FDRV void iflash_delay_loops(uint32_t n)
{
    for (volatile uint32_t i = 0; i < n; i++) { }
}

static FDRV int flash_wait_idle(uint32_t min_ms)
{
    FMC(0x08) |= 8u;                               /* busy / suspend handling on */
    iflash_delay_loops(min_ms * iflash_loops_per_ms);
    int rc = -1;
    for (uint32_t n = 0; n < SPIN_MAX; n++)
        if ((flash_status() & 1u) == 0) { rc = 0; break; }
    FMC(0x08) &= ~8u;
    return rc;
}

static FDRV int erase_4k(uint32_t addr)            /* 4 KiB sector erase */
{
    FMC(0x14) = addr;
    fmc_cmd(0x06, 7, 0, 0x700, 0);                 /* WREN */
    fmc_cmd(0x20, 7, 0x17, 0x300, 0);              /* sector erase */
    return flash_wait_idle(ERASE_MIN_MS);
}

static FDRV int program_page(uint32_t addr, const uint32_t *src, uint32_t len)   /* len 4..256, %4 == 0 */
{
    /* order matters: WREN, address, first word, command,
       then: wait FIFO-not-full, stop if < 4 bytes left, push next word */
    fmc_cmd(0x06, 7, 0, 0x700, 0);                 /* WREN */
    FMC(0x14) = addr;
    FMC(0x04) = *src++;
    uint32_t left = len - 4u;
    fmc_cmd(0x02, 7, 0x17, 0x340, len - 1u);       /* page program, single line */
    for (;;) {
        if (spin_while_set(0x24, 8u)) return -1;   /* TX FIFO full */
        if (left < 4u) break;
        FMC(0x04) = *src++;
        left -= 4u;
    }
    return flash_wait_idle(PROG_MIN_MS);
}

FDRV int iflash_init(uint32_t clkdiv_reg)
{
    int quad = 0;
    FMC(0x44) = 1;
    uint32_t irq = cpu_psw_get() & 1u;
    cpu_gie_disable();
    if ((REG32(0x40010114u) & 0xFFFFu) != 0 && (REG32(CLK_SEL) & 1u))
        REG32(0x40021040u) = clkdiv_reg;            /* flash clock = PLL / 6 (80 MHz) */
    if ((REG32(0x400100F8u) & 1u) == 1u)           /* I/O mode */
        REG32(0x400100F8u) |= 4u;
    if ((REG32(0x40010114u) & 0xFu) < 0xDu)
        REG8(0x400100F8u) = (uint8_t)((REG8(0x400100F8u) & 0xFBu) | (((REG32(0x400100F8u) & 1u) ^ 1u) << 2));
    FMC(0x08) |= 1u;                               /* fmc_enable */
    fmc_exit_crm();
    if (flash_status() & 0x200u) {                 /* QE already set (the vendor firmware sets it) -> quad XIP */
        FMC(0x30) = 0xA37A75EBu;
        FMC(0x34) = 0xA006u;
        FMC(0x38) = 0x0280190Bu;
        FMC(0x3C) = 0x000CDDDFu;
        FMC(0x38) = (FMC(0x38) & 0xFFE0001Fu) | 64000u;
        FMC(0x4C) = 0xA0u;
        quad = 1;
    }                                              /* else: keep the bootloader's read mode, never write SR */
    REG32(RST_CTRL) = (REG32(RST_CTRL) & ~4u) | 4u;
    if (irq) cpu_gie_enable();
    FMC(0x44) = 0;
    return quad;
}

/* Erase + program [addr, addr+len). addr must be 4 KiB aligned, src 4-byte aligned and readable up to
 * len rounded up to 4. Interrupts are off per 4 KiB sector, on in between. Returns 0 or <0. */
FDRV int iflash_write(uint32_t addr, const void *src, uint32_t len)
{
    if (addr < IFLASH_WRITE_MIN || addr >= IFLASH_END || len == 0 || len > IFLASH_END - addr) return -1;
    if ((addr & 0xFFFu) || ((uint32_t)(uintptr_t)src & 3u)) return -2;
    if (iflash_loops_per_ms == 0) return -4;       /* not calibrated: refuse rather than risk a hang */

    const uint32_t *p = (const uint32_t *)src;
    uint32_t left = (len + 3u) & ~3u;
    while (left) {
        uint32_t irq = cpu_psw_get() & 1u;
        cpu_gie_disable();
        if (FMC(0x50) & 1u) fmc_exit_crm();
        int rc = erase_4k(addr);
        for (uint32_t off = 0; rc == 0 && off < 0x1000u && left; off += 256u) {
            uint32_t n = left < 256u ? left : 256u;
            rc = program_page(addr + off, p, n);
            p += n >> 2;
            left -= n;
        }
        if (irq) cpu_gie_enable();
        if (rc) return -3;
        addr += 0x1000u;
    }
    return 0;
}

/* Program only, no erase: [addr, addr+len) must already be erased (0xFF). addr, len and src 4-byte
 * aligned. Same page-program routine as iflash_write; interrupts are off per 256-byte page only
 * (a few ms), so the display and the link keep running. Returns 0 or <0. */
FDRV int iflash_program(uint32_t addr, const void *src, uint32_t len)
{
    if (addr < IFLASH_WRITE_MIN || addr >= IFLASH_END || len == 0 || len > IFLASH_END - addr) return -1;
    if ((addr & 3u) || (len & 3u) || ((uint32_t)(uintptr_t)src & 3u)) return -2;
    if (iflash_loops_per_ms == 0) return -4;

    const uint32_t *p = (const uint32_t *)src;
    while (len) {
        uint32_t room = 256u - (addr & 0xFFu);      /* never cross a page boundary */
        uint32_t n = len < room ? len : room;
        uint32_t irq = cpu_psw_get() & 1u;
        cpu_gie_disable();
        if (FMC(0x50) & 1u) fmc_exit_crm();
        int rc = program_page(addr, p, n);
        if (irq) cpu_gie_enable();
        if (rc) return -3;
        addr += n;
        p += n >> 2;
        len -= n;
    }
    return 0;
}
