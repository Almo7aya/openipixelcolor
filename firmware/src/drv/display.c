/* 32x32 RGB LED matrix driver - the same register sequence the vendor firmware uses (docs/HARDWARE.md).
 *
 *  fb (logical, 32x32 RGB) --orientation/gamma--> plane buffer [32 rows][12 slots][32 px]
 *  LDMA 0x4000A000 streams one row (0x180 B) into the LED controller (LEDC 0x40012000); IRQ 31 per row.
 *  DMA ch2 (req 0x15, paced by TMR6) writes the row address of each slot to P1.8-12 (GPIO byte 0x40010039).
 *  DMA ch1 (req 0x0C, paced by TMR3) writes the on-time of each slot to LEDC+0x1C (BCM weights).
 *  Pixel byte: R bit5 / G bit4 / B bit3 -> data pins P1.5/P1.4/P1.3.
 */
#include "display.h"
#include "../hal/regs.h"
#include "../hal/irq.h"
#include "../hal/cpu.h"

extern const uint8_t gamma_lut[256];

#define LEDC32(o)   REG32(0x40012000u + (o))
#define LEDC16(o)   REG16(0x40012000u + (o))
#define LEDC8(o)    REG8 (0x40012000u + (o))
#define LDMA32(o)   REG32(0x4000A000u + (o))
#define LDMA8(o)    REG8 (0x4000A000u + (o))
#define GDMA32(c,o) REG32(0x40008000u + (uint32_t)(c) * 0x20u + (o))
#define GDMA16(c,o) REG16(0x40008000u + (uint32_t)(c) * 0x20u + (o))
#define GDMA_EN     REG32(0x40008100u)
#define TMR32(n,o)  REG32(TIMER_BASE(n) + (o))
#define TMR16(n,o)  REG16(TIMER_BASE(n) + (o))
#define PINMUX_LEDC  REG32(0x40010100u)
#define PINMUX_LDATA REG32(0x40010104u)

#define DISP_IRQ    31u
#define ROWS        32u
#define SLOTS       12u
#define ROW_BYTES   (SLOTS * 32u)          /* 0x180 */
#define PLANE_BYTES (ROWS * ROW_BYTES)     /* 0x3000 */

/* DMA ring "+0x10 low16": its value for the display channels is unclear; the one place it is clearly
 * set deliberately (SPI-slave ring) uses the ring length, so we do the same. Set to 0 to try the other way. */
#define GDMA_RELOAD_IS_LEN 1

static const uint8_t  SLOT_BIT[SLOTS]   = {0, 7, 1, 7, 2, 7, 3, 6, 4, 5, 6, 7};
static const uint8_t  OE_SHIFT[SLOTS]   = {0, 4, 0, 3, 0, 2, 0, 1, 0, 0, 0, 5};   /* entry k is used by slot k+1 */
static const uint16_t BRIGHT_OE[16]     = {56, 63, 71, 80, 90, 102, 115, 130, 147, 166, 208, 261, 327, 409, 512, 640};

static uint8_t  planebuf[2][PLANE_BYTES] __attribute__((aligned(4)));
static uint8_t  row_tab[ROWS * SLOTS]    __attribute__((aligned(4)));
static uint32_t oe_tab[SLOTS]            __attribute__((aligned(4)));

static volatile uint8_t  scan_row;
static uint8_t *volatile scan_buf;
static volatile uint8_t  frame_ready;     /* a newly built buffer waits for the frame boundary */
static volatile uint8_t  newest;          /* index of the most recently built buffer */
/* Mappings 0..3 are successive 90-degree steps of the picture. The vendor default is 1; on this panel
 * that shows the image a quarter turn clockwise, so we default to 2 (= 1 rotated 90 deg anticlockwise). */
static uint8_t g_orientation = 2;
static uint8_t g_gamma = 1;

/* ---- LED data DMA ------------------------------------------------------------------------- */
static void ldma_load(const uint8_t *src)  /* one row, 0x180 bytes */
{
    REG32(CLK_GATE0) |= 0x10000u;
    LDMA8(0x1C) &= 0xFEu;
    LDMA8(0x00) = (uint8_t)((LDMA8(0x00) & 0xF0u) | 0x80u);
    LDMA32(0x14) = (uint32_t)(uintptr_t)src;
    LDMA32(0x04) = ROW_BYTES;
    LDMA32(0x10) = ROW_BYTES;
    LDMA32(0x0C) = 0;
}

static void ldma_kick(void)
{
    if (LDMA8(0x00) & 0x80u) LDMA32(0x0C) = 0;   /* ldma_write_reg0c(0) */
    LDMA8(0x1C) |= 1u;                            /* run */
}

static void display_isr(unsigned irq)              /* row interrupt (IRQ 31) */
{
    (void)irq;
    if (!(LDMA32(0x18) & 1u) || !(LDMA8(0x00) & 0x10u)) return;
    LDMA8(0x1C) &= 0xFEu;                          /* stop */
    LDMA8(0x18) |= 0x08u;                          /* clear flag 0 */
    if (scan_row == ROWS) {
        scan_row = 0;
        if (frame_ready) { scan_buf = planebuf[newest]; frame_ready = 0; }
    }
    ldma_load(scan_buf + (uint32_t)scan_row * ROW_BYTES);
    scan_row++;
    ldma_kick();
}

/* ---- general DMA ring, memory -> peripheral ---------------- */
static void gdma_ring_tx(unsigned ch, unsigned req, unsigned width, const void *mem, uint32_t periph, uint16_t len)
{
    GDMA_EN &= ~(1u << ch);
    for (unsigned o = 0; o < 0x20u; o += 4u) GDMA32(ch, o) = 0;
    REG32(CLK_GATE0) |= 0x8000u;
    GDMA32(ch, 0) = (GDMA32(ch, 0) & ~0x1Fu)   | (req & 0x1Fu);
    GDMA32(ch, 0) = (GDMA32(ch, 0) & ~0x60u)   | (1u << 5);        /* memory -> peripheral */
    GDMA32(ch, 0) &= ~0x80u;
    GDMA32(ch, 0) = (GDMA32(ch, 0) & ~0x300u)  | (width << 8);
    GDMA32(ch, 0) = (GDMA32(ch, 0) & ~0xC00u)  | (width << 10);
    GDMA32(ch, 0) = (GDMA32(ch, 0) & ~0x3000u) | (1u << 12);       /* source increments */
    GDMA32(ch, 0) &= ~0xC000u;                                      /* destination fixed */
    GDMA32(ch, 0) |= 0x200000u;                                     /* ring */
    GDMA32(ch, 0) |= 0x400000u;
    GDMA32(ch, 0x10) |= 0x10000u;
    GDMA16(ch, 0x10) = GDMA_RELOAD_IS_LEN ? len : 0;
    GDMA16(ch, 0x04) = len;
    GDMA32(ch, 0x14) = (uint32_t)(uintptr_t)mem;
    GDMA32(ch, 0x18) = periph;
    GDMA16(ch, 0x0C) = (uint16_t)(len + 4u);                        /* write index never caught up */
    GDMA_EN |= 1u << ch;
}

/* ---- display pacing timers -------- */
static void disp_timer_cfg(unsigned n, unsigned ev)
{
    TMR32(n, 0) &= ~1u;
    TMR32(n, 0) |= 0x1000u;
    TMR32(n, 0) &= ~2u;
    TMR32(n, 0) |= 0x10u;
    TMR32(n, 0) &= ~0x60u;
    TMR32(n, 0x10) = 99;
    TMR32(n, 0x0C) = 0xFFFF;
    TMR32(n, 0) |= 0x2000u;
    TMR32(n, 0) |= 0x8000u;
    TMR16(n, 0x04) &= 0xFF7Fu;
    TMR16(n, 0x1C) = (uint16_t)((TMR16(n, 0x1C) & 0xE1FFu) | (2u << 9));
    TMR16(n, 0x1C) = (uint16_t)((TMR16(n, 0x1C) & 0xFFF0u) | ((ev - 1u) & 0xFu));
    TMR16(n, 0x1C) &= 0xFFCFu;
    TMR16(n, 0x1C) &= 0xFE3Fu;                                     /* left unset by the vendor init, use 0 */
    if (n > 3u) {
        for (unsigned o = 0x22u; o >= 0x1Eu; o -= 2u)
            TMR16(n, o) = (uint16_t)((TMR16(n, o) & 0xFFF0u) | ((ev - 1u) & 0xFu));
        for (unsigned o = 0x22u; o >= 0x1Eu; o -= 2u)
            TMR16(n, o) &= 0xFFCFu;
        for (unsigned o = 0x1Eu; o <= 0x22u; o += 2u)
            TMR16(n, o) &= 0xFE3Fu;                                /* left unset by the vendor init, use 0 */
    }
    TMR32(n, 0) |= 0x10000u;
    TMR32(n, 0) &= ~8u;
    TMR32(n, 0) |= 0x100u;
    TMR32(n, 0) |= 0x1000000u;
}

static void disp_timer_start(unsigned n)
{
    TMR32(n, 0) |= 1u;
    TMR32(n, 0) |= 0x1000000u;
}

/* ---- LED controller ------------------ */
static void ledc_configure(void)
{
    LEDC8(0x04) &= 0xF8u;                                                       /* cfg[0] = 0 */
    LEDC16(0x10) = 0x14;                                                        /* cfg[2] = 20 */
    LEDC32(0x10) = (LEDC32(0x10) & 0xFFC0FFFFu) | (0x0Au << 16);                /* cfg[3] = 10, cfg[4] = 0 */
    LEDC32(0x14) = (LEDC32(0x14) & 0xF808u) | (1u << 4);                        /* cfg[5..8] = 0,0,0,1 */
    LEDC32(0x18) = (LEDC32(0x18) & 0xFC000000u) | 0x04000000u | (1u << 25) | (0x0Au << 20) | 0x20u;
    LEDC32(0x1C) = (LEDC32(0x1C) & 0xFFF00000u) | 0x40u;
    LEDC32(0x20) = (LEDC32(0x20) & 0xFE0FFF00u) | 0x04000000u | (0x14u << 20) | 0x1Eu;
    LEDC32(0x20) &= ~0x02000000u;                                               /* cfg[0x14] = 0 */
    LEDC8(0x24)  = (uint8_t)((LEDC8(0x24) & 0xE0u) | 0x0Fu);
    LEDC32(0x08) = (LEDC32(0x08) & 0xF0FFFFE0u) | 0x10u | (1u << 24);
}

/* ---- public API ------------------------------------------------------------------------------ */
void display_set_brightness(unsigned level)
{
    if (level > 15u) level = 15u;
    uint32_t d = BRIGHT_OE[level];
    for (unsigned i = 0; i < SLOTS; i++) oe_tab[i] = d >> OE_SHIFT[i];    /* DMA ch1 picks it up live */
}

void display_set_orientation(unsigned o) { g_orientation = (uint8_t)(o & 3u); }
void display_set_gamma(int on)           { g_gamma = on ? 1u : 0u; }

void display_init(void)
{
    for (unsigned k = 0; k < ROWS * SLOTS; k++) row_tab[k] = (uint8_t)(((k + 1u) / SLOTS) % ROWS);
    for (unsigned i = 0; i < 2u; i++) for (unsigned k = 0; k < PLANE_BYTES; k++) planebuf[i][k] = 0;
    display_set_brightness(13);                    /* default 80% -> level 13 */
    scan_row = 1;                                  /* row 0 is started below */
    scan_buf = planebuf[1];
    newest = 1;
    frame_ready = 0;

    /* DMA and pacing timers */
    gdma_ring_tx(2, 0x15, 0, row_tab, 0x40010039u, ROWS * SLOTS);   /* row address -> P1.8..12 */
    REG32(0x400100E8u) = (REG32(0x400100E8u) & ~0x20u) | 1u;        /* tmr_route_channel(10,0,1) */
    TMR32(6, 0) |= 0x10000u;                                         /* tmr_channel_ctl(6,0,6,{1}) */
    TMR32(6, 0) = (TMR32(6, 0) & ~0xE00u) | (1u << 9);
    disp_timer_cfg(6, 2);
    disp_timer_start(6);
    gdma_ring_tx(1, 0x0C, 2, oe_tab, 0x4001201Cu, SLOTS * 4u);       /* on-time -> LEDC+0x1C */
    REG32(0x400100DCu) = (REG32(0x400100DCu) & ~0x600u & ~0x800u) | 0x600u | 0x100u;   /* tmr_route_channel(1,3,1) */
    disp_timer_cfg(3, 3);
    disp_timer_start(3);

    /* LED controller, then start */
    irq_register(DISP_IRQ, display_isr);
    irq_enable(DISP_IRQ);
    PINMUX_LEDC = (PINMUX_LEDC & ~1u) | 1u;                   /* OE? on P0.18 */
    PINMUX_LEDC = (PINMUX_LEDC & ~4u) | 2u;                   /* sig24 on P0.16 */
    PINMUX_LEDC = (PINMUX_LEDC & ~0x10u) | 8u;                /* sig25 on P0.17 */
    PINMUX_LEDC = (4u << 17) | (PINMUX_LEDC & 0x1FFFFu);
    PINMUX_LDATA = 0xFF;                                      /* data on P1.0..7 */
    REG32(CLK_GATE0) |= 0x50000u;
    ledc_configure();
    irq_enable(DISP_IRQ);
    LDMA8(0x00) = (uint8_t)((LDMA8(0x00) & ~0x10u) | 0x10u);  /* LDMA IRQ enable */
    LDMA8(0x18) |= 0x08u;
    ldma_load(scan_buf);                                       /* row 0 */
    LEDC32(0x08) |= 1u << 28;                                  /* enable */
    LEDC8(0x0C) |= 1u;                                         /* run */
    ldma_kick();
}

static inline void src_coord(unsigned r, unsigned c, unsigned *sy, unsigned *sx)
{
    switch (g_orientation) {
    case 0:  *sy = r;       *sx = 31u - c; break;
    case 1:  *sy = 31u - c; *sx = 31u - r; break;
    case 2:  *sy = 31u - r; *sx = c;       break;
    default: *sy = c;       *sx = r;       break;
    }
}

int display_show(const uint8_t fb[32][32][3])
{
    if (frame_ready) return 0;                     /* previous frame not taken yet */
    uint8_t *dst = planebuf[newest ^ 1u];
    for (unsigned r = 0; r < ROWS; r++) {
        uint8_t *row = dst + r * ROW_BYTES;
        for (unsigned c = 0; c < 32u; c++) {
            unsigned sy, sx;
            src_coord(r, c, &sy, &sx);
            uint8_t R = fb[sy][sx][0], G = fb[sy][sx][1], B = fb[sy][sx][2];
            if (g_gamma) { R = gamma_lut[R]; G = gamma_lut[G]; B = gamma_lut[B]; }
            uint8_t v[8];
            for (unsigned b = 0; b < 8u; b++)
                v[b] = (uint8_t)((((R >> b) & 1u) << 5) | (((G >> b) & 1u) << 4) | (((B >> b) & 1u) << 3));
            for (unsigned s = 0; s < SLOTS; s++) row[s * 32u + c] = v[SLOT_BIT[s]];
        }
    }
    uint32_t irq = cpu_irq_save();
    newest ^= 1u;
    frame_ready = 1;
    cpu_irq_restore(irq);
    return 1;
}

void display_show_wait(const uint8_t fb[32][32][3])
{
    while (!display_show(fb)) { }
}

/* Stop the whole scan pipeline (panel goes dark) - the vendor firmware does the same
 * before writing flash during an update. Re-enabled only by a reboot. */
void display_suspend(void)
{
    TMR32(3, 0) &= ~1u;                        /* tmr_stop(3) */
    GDMA_EN &= ~(1u << 1);                     /* DMA req 0x0C (ch1) off */
    TMR32(6, 0) &= ~1u;                        /* tmr_stop(6) */
    GDMA_EN &= ~(1u << 2);                     /* DMA req 0x15 (ch2) off */
    LDMA8(0x00) &= (uint8_t)~0x10u;            /* ldma_set_irq_enable(0,0) */
    LDMA8(0x18) |= 0x08u;                      /* ldma_clear_flag(0) */
    LEDC32(0x08) &= ~(1u << 28);               /* ledc_set_enable(0) */
    LEDC8(0x0C) &= 0xFEu;                      /* ledc_set_run(0) */
    LDMA8(0x1C) &= 0xFEu;                      /* ldma_set_run(0) */
    irq_disable(DISP_IRQ);
}

void display_blank(void)
{
    for (unsigned i = 0; i < 2u; i++) for (unsigned k = 0; k < PLANE_BYTES; k++) planebuf[i][k] = 0;
}

uint32_t display_rows_scanned(void) { return scan_row; }
