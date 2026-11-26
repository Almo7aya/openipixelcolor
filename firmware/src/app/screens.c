/* Screens the panel shows by itself, all drawn with code (no bitmaps):
 *   logo     an open colour ring (the colours of the panel) around a pixel "i", with one pixel escaping
 *            through the gap (open). tools/sim/make_readme_images.py renders it for the README.
 *   boot     the ring draws itself, the "i" appears, the pixel flies out, then the version.
 *   welcome  first start (nothing saved yet): the logo, then "HELLO / CONNECT" with a Bluetooth sign,
 *            until something is chosen in the app or with the button.
 *   update   while a firmware update arrives: the ring fills with colour, pixels stream in through the
 *            gap, the percentage in the middle (the panel goes dark while each block is written).
 */
#include "screens.h"
#include "tables.h"
#include "fwinfo.h"
#include "ota.h"

#define RING_OUT2  740u      /* (2 x 13.6)^2 - doubled coordinates: pixel centres are odd numbers */
#define RING_EDGE2 600u      /* outer rim: a little dimmer, rounder on the LEDs */
#define RING_IN2   276u      /* (2 x 8.3)^2 */
#define GAP_A      210u      /* the opening, top right: angles 210..238 of 256 (0 = right, clockwise) */
#define GAP_B      238u
#define GAP_MID    224u

/* angle of (dx, dy) in 1/256 turns, 0 = right, clockwise on the screen (y grows downwards) */
static unsigned angle8(int dx, int dy)
{
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (!ax && !ay) return 0;
    int mn = ax < ay ? ax : ay, mx = ax < ay ? ay : ax;
    int t = mn * 256 / mx;                                  /* tangent within the octant, q8 */
    int a = (32 * t + 11 * t * (256 - t) / 256) / 256;      /* atan, 0..32 (error < 1/2) */
    if (ay > ax) a = 64 - a;
    if (dx < 0) a = 128 - a;
    if (dy < 0) a = 256 - a;
    return (unsigned)a & 255u;
}

static rgb_t dim(rgb_t c, unsigned k) { return gfx_scale(c, (uint8_t)(k > 255u ? 255u : k)); }

static void tiny_centered(int y, const char *s, rgb_t c) { gfx_tiny_text((32 - gfx_tiny_width(s)) / 2, y, s, c); }

void screens_logo(int cy2, unsigned reveal, unsigned spin, unsigned escape, unsigned ink)
{
    const unsigned arc = 256u - (GAP_B - GAP_A) - 1u;        /* length of the ring, in angle units */
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int X = 2 * x + 1 - 32, Y = 2 * y + 1 - cy2;
            unsigned d2 = (unsigned)(X * X + Y * Y);
            if (d2 <= RING_IN2 || d2 > RING_OUT2) continue;
            unsigned a = angle8(X, Y);
            if (a >= GAP_A && a <= GAP_B) continue;
            unsigned from = (a - GAP_B - 1u) & 255u;          /* 0 right after the gap, clockwise */
            if (from * 256u >= reveal * arc) continue;
            unsigned v = d2 > RING_EDGE2 ? 190u : 255u;
            gfx_px(x, y, gfx_hsv((uint8_t)(a + spin), 255, (uint8_t)v));
        }

    if (ink) {                                                /* the "i" */
        rgb_t c = dim(RGB(240, 244, 255), ink);
        int cy = cy2 / 2;
        gfx_rect(15, cy - 7, 2, 2, c, 1);                     /* dot */
        gfx_rect(14, cy - 4, 3, 1, c, 1);                     /* top serif */
        gfx_rect(15, cy - 4, 2, 9, c, 1);                     /* stem */
        gfx_rect(14, cy + 5, 4, 1, c, 1);                     /* foot */
    }

    if (reveal >= 256u) {                                     /* the loose pixel, on its way out of the gap */
        if (escape > 256u) escape = 256u;
        unsigned e = 256u - (256u - escape) * (256u - escape) / 256u;     /* ease out */
        int d16 = 168 + (int)(e * 104u / 256u);               /* distance from the centre, 1/16 px: 10.5 .. 17 */
        for (int k = 2; k >= 0; k--) {                        /* two fading copies behind it while it moves */
            int dk = d16 - k * 24;
            if (k && (escape >= 256u || dk < 168)) continue;
            int off = dk * 181 / 256;                         /* d / sqrt 2 */
            int px = 16 + (off + 8) / 16 - 1, py = cy2 / 2 - (off + 8) / 16 - 1;
            rgb_t c = gfx_hsv((uint8_t)(GAP_MID + spin), 50, 255);   /* nearly white, a hint of the gap's colour */
            gfx_rect(px, py, 2, 2, k ? dim(c, 90u / (unsigned)k) : c, 1);
        }
    }
}

void screens_boot(uint32_t t)
{
    gfx_fill(0);
    unsigned reveal = t >= 700u ? 256u : t * 256u / 700u;
    unsigned ink = t < 350u ? 0u : t >= 800u ? 255u : (t - 350u) * 255u / 450u;
    unsigned esc = t < 850u ? 0u : t >= 1300u ? 256u : (t - 850u) * 256u / 450u;
    screens_logo(26, reveal, t / 6u, esc, ink);                /* centre row 13: room for the version below */
    if (t >= 1050u) {
        unsigned v = t >= 1400u ? 255u : (t - 1050u) * 255u / 350u;
        tiny_centered(27, FW_VERSION_STR, dim(RGB(150, 155, 170), v));
    }
}

/* Bluetooth sign, 7 x 11, bit 6 = left column */
static const uint8_t BT[11] = { 0x08, 0x0C, 0x0A, 0x49, 0x2A, 0x1C, 0x2A, 0x49, 0x0A, 0x0C, 0x08 };

void screens_welcome(uint32_t now)
{
    uint32_t t = now % 8000u;
    gfx_fill(0);
    if (t < 4000u) {                                          /* the logo, colours turning, the pixel bobbing */
        unsigned esc = (unsigned)sin8[(uint8_t)(now / 10u)] + 1u;
        screens_logo(32, 256, now / 24u, esc, 255);
        return;
    }
    unsigned pulse = 120u + (unsigned)sin8[(uint8_t)(now / 6u)] * 135u / 255u;
    tiny_centered(1, "HELLO", gfx_hsv((uint8_t)(now / 20u), 140, 255));
    for (int y = 0; y < 32; y++)                              /* blue badge */
        for (int x = 0; x < 32; x++) {
            int X = 2 * x + 1 - 32, Y = 2 * y + 1 - 31;
            if (X * X + Y * Y <= 300) gfx_px(x, y, dim(RGB(20, 90, 255), pulse));
        }
    for (int r = 0; r < 11; r++)
        for (int c = 0; c < 7; c++)
            if (BT[r] & (0x40u >> c)) gfx_px(12 + c, 10 + r, RGB(255, 255, 255));
    tiny_centered(26, "CONNECT", RGB(120, 170, 255));
}

/* The update screen is the logo filling up: the ring starts dim and takes on its colours clockwise from
 * the gap as the firmware arrives, the part of the block on its way pulses, pixels stream in through the
 * gap (the opposite of the boot screen, where one flies out) and the percentage sits in the middle.
 * The panel goes dark while each block is written to flash and comes back with the ring further on. */
void screens_update(fb_t *fb, uint32_t now, uint32_t done, uint32_t total)
{
    gfx_target(fb);
    gfx_fill(0);
    if (done > total) done = total;
    uint32_t next = done + OTA_CHUNK > total ? total : done + OTA_CHUNK;
    const unsigned arc = 256u - (GAP_B - GAP_A) - 1u;
    unsigned filled = total ? (unsigned)(done * arc / total) : 0u;      /* images are < 1 MiB: no overflow */
    unsigned coming = total ? (unsigned)(next * arc / total) : 0u;
    unsigned spin = now / 16u;
    unsigned pulse = 70u + (unsigned)sin8[(uint8_t)(now / 3u)] * 150u / 255u;

    for (int y = 0; y < 32; y++)                              /* the ring */
        for (int x = 0; x < 32; x++) {
            int X = 2 * x + 1 - 32, Y = 2 * y + 1 - 32;
            unsigned d2 = (unsigned)(X * X + Y * Y);
            if (d2 <= RING_IN2 || d2 > RING_OUT2) continue;
            unsigned a = angle8(X, Y);
            if (a >= GAP_A && a <= GAP_B) continue;
            unsigned from = (a - GAP_B - 1u) & 255u;
            unsigned v = from < filled ? 255u : from < coming ? pulse : 26u;
            rgb_t c = gfx_hsv((uint8_t)(a + spin), 255, (uint8_t)(d2 > RING_EDGE2 ? v * 3u / 4u : v));
            if (from < filled && from + 2u >= filled)           /* the leading edge shines a little */
                c = RGB(((c >> 16) + 255u) / 2u, (((c >> 8) & 255u) + 255u) / 2u, ((c & 255u) + 255u) / 2u);
            gfx_px(x, y, c);
        }

    for (unsigned k = 0; k < 4u; k++) {                       /* pixels streaming in through the gap */
        static const int8_t SIDE[4] = { -20, 6, 22, -6 };   /* across the stream, 1/16 px */
        uint32_t t = (now + k * 290u) % 1160u;              /* 0 .. 1 of the way, 1.16 s */
        int d16 = 352 - (int)(t * 208u / 1160u);            /* distance from the centre: 22 .. 9 px */
        for (int trail = 0; trail < 2; trail++) {
            int dd = d16 + trail * 28;
            if (dd > 352) continue;
            int along = dd * 181 / 256, side = SIDE[k] * 181 / 256;     /* / sqrt 2 */
            int px = 16 + (along + side + 8) / 16 - 1, py = 16 - (along - side + 8) / 16;
            rgb_t c = gfx_hsv((uint8_t)(GAP_MID + spin + k * 20u), trail ? 200 : 80, 255);
            gfx_px(px, py, trail ? dim(c, 90) : c);
        }
    }

    uint32_t pct = total ? done * 100u / total : 0u;
    if (pct >= 100u) {                                        /* done: a tick */
        static const int8_t TICK[7][2] = { { 11, 16 }, { 12, 17 }, { 13, 18 }, { 14, 17 }, { 15, 16 }, { 16, 15 }, { 17, 14 } };
        for (int i = 0; i < 7; i++) gfx_rect(TICK[i][0] + 1, TICK[i][1] - 1, 2, 2, RGB(90, 255, 140), 1);
        return;
    }
    char s[3];                                                /* the percentage (the ring says it's a share) */
    unsigned n = 0;
    if (pct >= 10u) s[n++] = (char)('0' + pct / 10u);
    s[n++] = (char)('0' + pct % 10u);
    s[n] = 0;
    gfx_text((32 - gfx_text_width(s)) / 2, 12, s, RGB(255, 255, 255));
}
