/* Screens the panel shows by itself, all drawn with code (no bitmaps). The logo: an open colour ring (the
 * colours of the panel) around a pixel "i", with one pixel escaping through the gap at the top right (open).
 * The ring is computed, so it can draw itself, fill up, shrink and grow. Two sizes: the big ring for the
 * logo and firmware updates, a small ring at the top with a word below it for everyday things.
 *   boot       the ring draws itself, the "i" rises and its dot drops in, the pixel escapes; the logo moves
 *              up and "open", "ipixel", "color" and the version step through the band below
 *   welcome    first start (nothing saved yet): the small logo, "open ipixel color" scrolling, and between
 *              two passes the "i" flips into the Bluetooth sign (connect to me)
 *   update     a firmware update arrives: the ring fills with colour clockwise from the gap, pixels stream
 *              in through the gap, big digits in the middle (dark while each block is written)
 *   connected  an app connected: the loose pixel flies back in, a light ring pulses out, Bluetooth sign
 *   saving     an animation is being stored: the small ring fills, an arrow drops in, the percentage; then
 *              a tick and "saved"
 * Text is the 5x7 font (never smaller), words step in or scroll, movement is eased. Integer maths only.
 * The design prototype is design/logos/logos.c (same drawing, renders GIFs).
 */
#include "screens.h"
#include "tables.h"
#include "fwinfo.h"
#include "ota.h"

_Static_assert(GFX_W == 32 && GFX_H == 32, "the screens are laid out for a 32 x 32 panel: lay them out for this size");

/* ---- small maths on 0..256 ---------------------------------------------------------------------- */
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int prog(int t, int a, int d) { return t <= a ? 0 : t >= a + d ? 256 : (t - a) * 256 / d; }
static int ease_out(int p) { int q = 256 - p; return 256 - q * q / 256; }
static int ease_in(int p) { return p * p / 256; }
static int ease_io(int p) { return p < 128 ? ease_in(2 * p) / 2 : 128 + ease_out(2 * p - 256) / 2; }
static int back_out(int p) { int q = p - 256, q2 = q * q / 256, q3 = q2 * q / 256; return 256 + 692 * q3 / 256 + 436 * q2 / 256; }
static int sinx(int phase) { return (int)sin8[phase & 255] - 128; }
static int lerp(int a, int b, int p) { return a + (b - a) * p / 256; }

#define WHITE RGB(255, 255, 255)
#define INK   RGB(240, 244, 255)                        /* the "i": white with a breath of blue */
#define BLUE  RGB(76, 140, 255)
#define GREEN RGB(60, 255, 122)
#define GREY  RGB(150, 156, 168)
static rgb_t dim(rgb_t c, int k) { return gfx_scale(c, (uint8_t)clampi(k, 0, 255)); }
static rgb_t mix(rgb_t a, rgb_t b, int p)
{
    p = clampi(p, 0, 256);
    return RGB(lerp((int)((a >> 16) & 255u), (int)((b >> 16) & 255u), p), lerp((int)((a >> 8) & 255u), (int)((b >> 8) & 255u), p),
               lerp((int)(a & 255u), (int)(b & 255u), p));
}
static void fade_all(int k) { for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) gfx_px(x, y, dim(gfx_get(x, y), k)); }

/* ---- text: the 5x7 font, a colour per letter; words stepping through a band ---------------------- */
static void text_cols(int x, int y, const char *s, const rgb_t *cols, int n)
{
    for (int i = 0; s[i]; i++) x += gfx_char(x, y, s[i], cols[i % n]);
}
/* in from below over 300 ms at t_in; out at the top the same way at t_out (= the next word's t_in), so
   the two move together 8 rows apart; t_out < 0: stays */
static void band_word(int y, const char *w, int t, int t_in, int t_out, const rgb_t *cols, int n)
{
    if (t < t_in || (t_out >= 0 && t >= t_out + 300)) return;
    int off = 8 - 8 * ease_out(prog(t, t_in, 300)) / 256;
    if (t_out >= 0 && t >= t_out) off = -8 * ease_out(prog(t, t_out, 300)) / 256;
    gfx_clip(0, y, 32, y + 7);
    text_cols((32 - gfx_text_width(w)) / 2, y + off, w, cols, n);
    gfx_noclip();
}
static void ring_hues(unsigned spin, rgb_t out[5])     /* five colours taken round the ring, for "color" */
{
    for (int i = 0; i < 5; i++) out[i] = gfx_hsv((uint8_t)((unsigned)i * 48u + spin), 220, 255);
}

/* big digits, 6 x 10 with a 2 px stroke (a row = 6 bits, bit 5 = left), 2 px apart, centred on cx */
static const uint8_t DIGIT[10][10] = {
    { 0x1E, 0x3F, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x1E }, { 0x0C, 0x1C, 0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x3F },
    { 0x1E, 0x3F, 0x33, 0x03, 0x07, 0x0E, 0x1C, 0x38, 0x3F, 0x3F }, { 0x1E, 0x3F, 0x33, 0x03, 0x0E, 0x0F, 0x03, 0x33, 0x3F, 0x1E },
    { 0x07, 0x0F, 0x1B, 0x33, 0x33, 0x3F, 0x3F, 0x03, 0x03, 0x03 }, { 0x3F, 0x3F, 0x30, 0x3E, 0x3F, 0x03, 0x03, 0x33, 0x3F, 0x1E },
    { 0x1E, 0x3F, 0x30, 0x3E, 0x3F, 0x33, 0x33, 0x33, 0x3F, 0x1E }, { 0x3F, 0x3F, 0x03, 0x07, 0x0E, 0x0C, 0x1C, 0x18, 0x18, 0x18 },
    { 0x1E, 0x3F, 0x33, 0x33, 0x1E, 0x3F, 0x33, 0x33, 0x3F, 0x1E }, { 0x1E, 0x3F, 0x33, 0x33, 0x3F, 0x1F, 0x03, 0x03, 0x3F, 0x1E },
};
static void digits(int cx, int y, unsigned value, rgb_t c)
{
    unsigned d[2], n = 0;
    if (value > 99u) value = 99u;
    if (value >= 10u) d[n++] = value / 10u;
    d[n++] = value % 10u;
    int x = cx - (int)(n * 8u - 2u) / 2;
    for (unsigned i = 0; i < n; i++, x += 8)
        for (int r = 0; r < 10; r++) for (int k = 0; k < 6; k++) if (DIGIT[d[i]][r] & (0x20u >> k)) gfx_px(x + k, y + r, c);
}

/* a 2 px thick tick through three points, drawn up to `len` pixels along its path */
static void tick(int x0, int y0, int x1, int y1, int x2, int y2, int len, rgb_t c)
{
    int px[2][3] = { { x0, x1, x2 }, { y0, y1, y2 } }, done = 0;
    for (int seg = 0; seg < 2; seg++) {
        int ax = px[0][seg], ay = px[1][seg], bx = px[0][seg + 1], by = px[1][seg + 1];
        int dx = bx > ax ? bx - ax : ax - bx, dy = by > ay ? ay - by : by - ay, sx = ax < bx ? 1 : -1, sy = ay < by ? 1 : -1, err = dx + dy;
        for (;;) {
            if (done++ >= len) return;
            gfx_px(ax, ay, c); gfx_px(ax, ay + 1, c);
            if (ax == bx && ay == by) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; ax += sx; }
            if (e2 <= dx) { err += dx; ay += sy; }
        }
    }
}

/* Bluetooth signs: 7 x 11 (bit 6 = left) and 5 x 7 (bit 4 = left); scale_y squashes them to the middle row */
static const uint8_t BT7[11] = { 0x08, 0x0C, 0x0A, 0x49, 0x2A, 0x1C, 0x2A, 0x49, 0x0A, 0x0C, 0x08 };
static const uint8_t BT5[7] = { 0x04, 0x06, 0x15, 0x0E, 0x15, 0x06, 0x04 };
static void rune7(int cx, int cy, int scale_y, rgb_t c)
{
    for (int r = 0; r < 11; r++) for (int k = 0; k < 7; k++) if (BT7[r] & (0x40u >> k)) gfx_px(cx - 3 + k, cy + (r - 5) * scale_y / 256, c);
}
static void rune5(int cx, int cy, int scale_y, rgb_t c)
{
    for (int r = 0; r < 7; r++) for (int k = 0; k < 5; k++) if (BT5[r] & (0x10u >> k)) gfx_px(cx - 2 + k, cy + (r - 3) * scale_y / 256, c);
}

/* =================================================================================================
 * The ring. Doubled coordinates (pixel centres are odd numbers), radii in 1/10 doubled px. Big: outer
 * 13.6 px, inner 8.3 px; small: 9.6 / 5.8 px. Gap at the top right (angles 210..238 of 256, 0 = right,
 * clockwise); hues go round the ring and turn with `spin`.
 * ================================================================================================= */
#define BIG_RO   272
#define BIG_RI   166
#define SMALL_RO 192
#define SMALL_RI 116
#define GAP_A    210u
#define GAP_B    238u
#define ARC      ((int)(256u - (GAP_B - GAP_A) - 1u))

static unsigned angle8(int dx, int dy)                  /* 1/256 turns, 0 = right, clockwise (y down) */
{
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (!ax && !ay) return 0;
    int mn = ax < ay ? ax : ay, mx = ax < ay ? ay : ax;
    int t = mn * 256 / mx;
    int a = (32 * t + 11 * t * (256 - t) / 256) / 256;
    if (ay > ax) a = 64 - a;
    if (dx < 0) a = 128 - a;
    if (dy < 0) a = 256 - a;
    return (unsigned)a & 255u;
}

typedef struct { int cy2, ro, ri; } ring_t;
static ring_t ring_at(int p)                            /* 0 = big and centred, 256 = small near the top */
{
    ring_t r = { lerp(32, 22, p), lerp(BIG_RO, SMALL_RO, p), lerp(BIG_RI, SMALL_RI, p) };
    return r;
}

/* how each part of the ring lights up (from = 0 right after the gap .. ARC-1 just before it):
   reveal  drawn up to there (0..256), the drawing tip white-hot
   filled  full colour up to there, then `pulse` up to `coming`, then dim (updates, saving)
   sweep   >= 0: a light running round (done), flash > 0: everything whitened */
typedef struct { int reveal, filled, coming, pulse, sweep, flash; } look_t;
static int ring_v(const look_t *l, int from)
{
    int tip = l->reveal * ARC / 256;
    if (from >= tip) return 0;
    if (l->reveal < 256 && from >= tip - 10) return 255 + 170 * (10 - (tip - from)) / 10;
    int v = from < l->filled ? 255 : from < l->coming ? l->pulse : 30;
    if (from < l->filled && from + 3 >= l->filled && l->filled < ARC) v = 365;          /* the leading edge shines */
    if (l->sweep >= 0) { int dd = l->sweep - from; if (dd >= 0 && dd < 24) v = 255 + 230 * (24 - dd) / 24; }
    return v + l->flash;
}
static void ring(ring_t g, unsigned spin, const look_t *l)
{
    int ro2 = g.ro * g.ro / 100, ri2 = g.ri * g.ri / 100, edge2 = (g.ro - 26) * (g.ro - 26) / 100;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int X = 2 * x + 1 - 32, Y = 2 * y + 1 - g.cy2, d2 = X * X + Y * Y;
            if (d2 <= ri2 || d2 > ro2) continue;
            unsigned a = angle8(X, Y);
            if (a >= GAP_A && a <= GAP_B) continue;
            int v = ring_v(l, (int)((a - GAP_B - 1u) & 255u));
            if (v <= 0) continue;
            int w = v > 255 ? v - 255 : 0;                  /* above 255: whitened */
            rgb_t c = gfx_hsv((uint8_t)(a + spin), 255, (uint8_t)(d2 > edge2 ? (v > 255 ? 255 : v) * 3 / 4 : (v > 255 ? 255 : v)));
            gfx_px(x, y, w ? mix(c, WHITE, w) : c);
        }
}
static const look_t FULL = { 256, ARC, ARC, 0, -1, 0 };

/* the "i": big (13 rows) or small (8 rows); grow 0..256 raises the stem from its foot; dot_dy: the dot's
   offset (it drops in), -99 = no dot yet */
static void letter_i(int cy, int small, int grow, int dot_dy, rgb_t c)
{
    if (grow <= 0) return;
    if (!small) {
        int h = 10 * clampi(grow, 0, 256) / 256;
        gfx_rect(14, cy + 5, 4, 1, c, 1);
        if (h > 1) gfx_rect(15, cy + 5 - h, 2, h, c, 1);
        if (grow >= 256) gfx_rect(14, cy - 4, 3, 1, c, 1);
        if (dot_dy > -99) gfx_rect(15, cy - 7 + dot_dy, 2, 2, c, 1);
    } else {
        int h = 6 * clampi(grow, 0, 256) / 256;
        gfx_rect(14, cy + 3, 4, 1, c, 1);
        if (h > 1) gfx_rect(15, cy + 3 - h, 2, h, c, 1);
        if (grow >= 256) gfx_rect(14, cy - 2, 3, 1, c, 1);
        if (dot_dy > -99) gfx_rect(15, cy - 5 + dot_dy, 2, 1, c, 1);
    }
}
static void small_i_squashed(int cy, int k)            /* k 0..256: squashed to its middle row (a flip) */
{
    if (k >= 256) { letter_i(cy, 1, 256, 0, INK); return; }
    int h = 4 * k / 256;
    gfx_clip(0, cy - h - 1, 32, cy + h + 1);
    letter_i(cy, 1, 256, 0, INK);
    gfx_noclip();
}

/* the loose pixel on the 45-degree line through the gap; out 0..256: 0 = in the gap, 256 = well outside
   (beyond: further); trail > 0 copies behind it moving out, < 0 moving in */
static void loose_pixel(ring_t g, int out, int trail, unsigned spin, int k)
{
    int r16 = g.ro * 8 / 10, d0 = r16 * 70 / 100, d1 = r16 * 128 / 100;
    rgb_t base = gfx_hsv((uint8_t)(224u + spin), 50, 255);
    for (int s = trail ? 2 : 0; s >= 0; s--) {
        int d16 = lerp(d0, d1, out) - s * trail;
        if (trail > 0 && d16 < d0 && s) continue;
        int off = d16 * 181 / 256;                          /* / sqrt 2 */
        int px = 16 + (off + 8) / 16 - 1, py = g.cy2 / 2 - (off + 8) / 16 - 1;
        gfx_rect(px, py, 2, 2, dim(base, s ? k * (3 - s) / 5 : k), 1);
    }
}

/* ---- the screens -------------------------------------------------------------------------------- */
void screens_logo(void)
{
    gfx_fill(0);
    ring_t g = ring_at(0);
    ring(g, 0, &FULL);
    letter_i(16, 0, 256, 0, INK);
    loose_pixel(g, 256, 0, 0, 255);
}

void screens_boot(uint32_t ms)
{
    int t = (int)ms;
    gfx_fill(0);
    unsigned spin = ms / 6u;
    int shrink = ease_io(prog(t, 1350, 380));
    ring_t g = ring_at(shrink);
    look_t l = FULL;
    l.reveal = ease_out(prog(t, 0, 650));
    ring(g, spin, &l);
    int dp = prog(t, 560, 320);
    letter_i(g.cy2 / 2, shrink >= 128, ease_out(prog(t, 330, 300)), dp ? -7 + 7 * back_out(dp) / 256 : -99, INK);
    if (t >= 880) {                                     /* a nudge back, then out through the gap */
        int out = t < 980 ? -20 * prog(t, 880, 100) / 256 : -20 + 276 * ease_out(prog(t, 980, 360)) / 256;
        loose_pixel(g, out, t < 1340 ? 22 : 0, spin, ease_out(prog(t, 880, 100)));
    }
    static const rgb_t ink[1] = { INK }, grey[1] = { GREY };
    rgb_t hues[5];
    ring_hues(spin, hues);
    band_word(24, "open", t, 1700, 2150, ink, 1);
    band_word(24, "ipixel", t, 2150, 2600, ink, 1);
    band_word(24, "color", t, 2600, 3050, hues, 5);
    band_word(24, "v" FW_VERSION_STR, t, 3050, -1, grey, 1);
}

#define WELCOME_TEXT   "open ipixel color"
#define WELCOME_MS_PX  32u
#define WELCOME_BT_MS  1400
void screens_welcome(uint32_t now)
{
    gfx_fill(0);
    unsigned spin = now / 22u;
    ring_t g = ring_at(256);
    int cy = g.cy2 / 2;
    ring(g, spin, &FULL);
    int scroll = (gfx_text_width(WELCOME_TEXT) + 32) * (int)WELCOME_MS_PX;
    int lt = (int)(now % (uint32_t)(scroll + WELCOME_BT_MS));
    if (lt < scroll) {                                  /* the name scrolls through the band */
        rgb_t cols[17], hues[5];
        ring_hues(spin, hues);
        for (int i = 0; i < 17; i++) cols[i] = i < 12 ? INK : hues[i - 12];
        gfx_clip(0, 24, 32, 31);
        text_cols(32 - lt / (int)WELCOME_MS_PX, 24, WELCOME_TEXT, cols, 17);
        gfx_noclip();
        letter_i(cy, 1, 256, 0, INK);
    } else {                                            /* the "i" flips into the Bluetooth sign and back */
        int b = lt - scroll;
        if (b < 120) small_i_squashed(cy, 256 - 206 * prog(b, 0, 120) / 256);
        else if (b < WELCOME_BT_MS - 240) rune5(16, cy, b < 240 ? 50 + 206 * prog(b, 120, 120) / 256 : 256, BLUE);
        else if (b < WELCOME_BT_MS - 120) rune5(16, cy, 256 - 206 * prog(b, WELCOME_BT_MS - 240, 120) / 256, BLUE);
        else small_i_squashed(cy, 50 + 206 * prog(b, WELCOME_BT_MS - 120, 120) / 256);
    }
    loose_pixel(g, 150 + 100 * sinx((int)(now % 1800u * 256u / 1800u)) / 128, 0, spin, 255);
}

/* pixels flying in through the gap (updates) */
static void stream_in(ring_t g, uint32_t now, unsigned spin)
{
    static const int8_t SIDE[4] = { -20, 6, 22, -6 };      /* across the stream, 1/16 px */
    for (unsigned k = 0; k < 4u; k++) {
        int p = (int)((now + k * 290u) % 1160u), d16 = 352 - p * 208 / 1160;
        for (int tr = 0; tr < 2; tr++) {
            int dd = d16 + tr * 28;
            if (dd > 352) continue;
            int along = dd * 181 / 256, side = SIDE[k] * 181 / 256;
            int px = 16 + (along + side + 8) / 16 - 1, py = g.cy2 / 2 - (along - side + 8) / 16;
            rgb_t c = gfx_hsv((uint8_t)(224u + spin + k * 20u), tr ? 200 : 80, 255);
            gfx_px(px, py, tr ? dim(c, 90) : c);
        }
    }
}

void screens_update(fb_t *fb, uint32_t now, uint32_t done, uint32_t total)
{
    gfx_target(fb);
    gfx_fill(0);
    if (done > total) done = total;
    uint32_t next = done + OTA_CHUNK > total ? total : done + OTA_CHUNK;
    unsigned spin = now / 16u;
    ring_t g = ring_at(0);
    look_t l = FULL;
    l.filled = total ? (int)(done * (uint32_t)ARC / total) : 0;     /* images are < 1 MiB: no overflow */
    l.coming = total ? (int)(next * (uint32_t)ARC / total) : 0;
    l.pulse = 80 + 150 * (sinx((int)(now % 760u * 256u / 760u)) + 128) / 255;
    ring(g, spin, &l);
    uint32_t pct = total ? done * 100u / total : 0u;
    if (pct >= 100u) { tick(10, 16, 14, 20, 22, 12, 99, GREEN); return; }
    stream_in(g, now, spin);
    digits(16, 11, pct, WHITE);
}

void screens_connected(uint32_t ms)
{
    int t = (int)ms, hit = 360;
    gfx_fill(0);
    unsigned spin = ms / 6u;
    ring_t g = ring_at(0);
    look_t l = FULL;
    if (t >= hit && t < hit + 320) l.flash = 200 * (256 - prog(t, hit, 320)) / 256;
    ring(g, spin, &l);
    if (t < hit) {                                      /* the loose pixel flies back in through the gap */
        letter_i(16, 0, 256, 0, INK);
        loose_pixel(g, 256 - 560 * ease_in(prog(t, 40, hit - 40)) / 256, -26, spin, 255);
    } else {
        int r = lerp(10, 60, ease_out(prog(t, hit, 360))), k = 255 - 255 * prog(t, hit, 360) / 256;
        if (k > 0)                                      /* a light ring pulsing out, 1/2 px */
            for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
                int X = 2 * x + 1 - 32, Y = 2 * y + 1 - 32, d2 = X * X + Y * Y;
                if (d2 >= (r - 3) * (r - 3) && d2 <= r * r) gfx_px(x, y, mix(gfx_get(x, y), WHITE, k));
            }
        rune7(16, 16, clampi(back_out(prog(t, hit, 200)), 0, 280), BLUE);
    }
    if (t > 1150) fade_all(255 - 255 * prog(t, 1150, 250) / 256);
}

static void arrow_down(int cx, int y, rgb_t c)         /* 5 wide, 6 tall */
{
    gfx_rect(cx, y, 1, 3, c, 1);
    gfx_rect(cx - 2, y + 3, 5, 1, c, 1);
    gfx_rect(cx - 1, y + 4, 3, 1, c, 1);
    gfx_px(cx, y + 5, c);
}

void screens_saving(uint32_t now, uint32_t done, uint32_t total, int done_ms)
{
    gfx_fill(0);
    unsigned spin = now / 16u;
    ring_t g = ring_at(256);
    int cy = g.cy2 / 2;
    uint32_t pct = total ? (done > total ? 100u : done * 100u / total) : 0u;   /* total <= 640 KiB: no overflow */
    look_t l = FULL;
    l.filled = (int)pct * ARC / 100;
    l.coming = clampi((int)pct + 18, 0, 100) * ARC / 100;
    l.pulse = 80 + 150 * (sinx((int)(now % 760u * 256u / 760u)) + 128) / 255;
    if (done_ms >= 0) { l.filled = l.coming = ARC; l.sweep = done_ms < 340 ? ARC * done_ms / 300 : -1; }
    ring(g, spin, &l);
    static const rgb_t ink[1] = { INK }, green[1] = { GREEN };
    if (done_ms < 0) {
        int p = (int)(now % 640u), y = cy - 8 + 9 * ease_in(p * 256 / 640) / 256;     /* drops through the hole */
        int k = p < 120 ? 255 * p / 120 : p > 520 ? 255 * (640 - p) / 120 : 255;
        gfx_clip(10, cy - 5, 22, cy + 6);
        arrow_down(16, y, dim(INK, k));
        gfx_noclip();
        char s[5];
        unsigned n = 0;
        if (pct >= 10u) s[n++] = (char)('0' + pct / 10u % 10u);
        if (pct >= 100u) { s[0] = '1'; s[1] = '0'; n = 2; }
        s[n++] = (char)('0' + pct % 10u);
        s[n++] = '%';
        s[n] = 0;
        band_word(24, s, 1000, 0, -1, ink, 1);
    } else {
        if (done_ms >= 200) tick(13, cy, 15, cy + 2, 19, cy - 2, (done_ms - 200) / 25, GREEN);
        band_word(24, "100%", done_ms, -300, 200, ink, 1);
        band_word(24, "saved", done_ms, 200, -1, green, 1);
    }
}
