/* The ring logo, round 3 (design prototype; drawn with the firmware's gfx code and integer maths only, so it
 * moves into firmware/src/app/screens.c as it is). Writes logos.bin: [32-byte label "<set> <screen>"]
 * [32*32*3] per frame, 40 ms apart.
 *
 *   gcc -O2 -I../../firmware/src/app logos.c ../../firmware/src/app/{gfx,tables,font5x7}.c -o logos
 *   ./logos logos.bin && python make_logos.py logos.bin .
 *
 * The mark (from the first logo): an open colour ring, the colours of the panel, with a white pixel "i" in
 * the middle and one pixel escaping through the gap at the top right ("open"). The ring is computed, not a
 * bitmap, so it can draw itself, fill up, shrink and grow.
 * Rules from the research (README.md): text at least 7 px tall (the 5x7 font), never the 3x5 font; words
 * step in one at a time or scroll; progress digits 10 px tall with a 2 px stroke; 40 ms frames, eased motion. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "gfx.h"
#include "tables.h"

#define FRAME_MS 40
static fb_t fb;
static FILE *out;
static void snap(const char *set, const char *screen)
{
    char n[32] = { 0 };
    snprintf(n, sizeof n, "%s %s", set, screen);
    fwrite(n, 1, 32, out);
    fwrite(fb, 1, sizeof fb, out);
}

/* ---- maths on 0..256 ---------------------------------------------------------------------------- */
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int prog(int t, int a, int d) { return t <= a ? 0 : t >= a + d ? 256 : (t - a) * 256 / d; }
static int ease_out(int p) { int q = 256 - p; return 256 - q * q / 256; }
static int ease_in(int p) { return p * p / 256; }
static int ease_io(int p) { return p < 128 ? ease_in(2 * p) / 2 : 128 + ease_out(2 * p - 256) / 2; }
static int back_out(int p) { int q = p - 256, q2 = q * q / 256, q3 = q2 * q / 256; return 256 + 692 * q3 / 256 + 436 * q2 / 256; }
static int sinx(int phase) { return (int)sin8[phase & 255] - 128; }          /* -128..127 */
static int lerp(int a, int b, int p) { return a + (b - a) * p / 256; }

#define WHITE RGB(255, 255, 255)
#define INK   RGB(240, 244, 255)                        /* the "i": white with a breath of blue */
#define BLUE  RGB(76, 140, 255)
#define GREEN RGB(60, 255, 122)
static rgb_t dim(rgb_t c, int k) { return gfx_scale(c, (uint8_t)clampi(k, 0, 255)); }
static rgb_t mix(rgb_t a, rgb_t b, int p)
{
    p = clampi(p, 0, 256);
    return RGB(lerp((a >> 16) & 255, (b >> 16) & 255, p), lerp((a >> 8) & 255, (b >> 8) & 255, p), lerp(a & 255, b & 255, p));
}
static void fade_all(int k) { for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) gfx_px(x, y, dim(gfx_get(x, y), k)); }

/* ---- the panel font (5x7, trimmed like gfx_text) -------------------------------------------------- */
static int span(char ch, int *first)
{
    int c = (ch < 32 || ch > 126) ? '?' : ch;
    const uint8_t *g = font5x7[c - 32];
    int f = 0, l = 4;
    while (f < 4 && !g[f]) f++;
    while (l > f && !g[l]) l--;
    *first = f;
    return l - f + 1;
}
static int text_w(const char *s)
{
    int w = 0, f;
    for (; *s; s++) w += ((*s == ' ') ? 3 : span(*s, &f)) + 1;
    return w ? w - 1 : 0;
}
static int text(int x, int y, const char *s, const rgb_t *colours, int n)
{
    for (int i = 0; s[i]; i++) {
        if (s[i] == ' ') { x += 4; continue; }
        int f, w = span(s[i], &f);
        const uint8_t *g = font5x7[s[i] - 32];
        for (int cx = 0; cx < w; cx++) for (int cy = 0; cy < 7; cy++) if (g[f + cx] & (1u << cy)) gfx_px(x + cx, y + cy, colours[i % n]);
        x += w + 1;
    }
    return x;
}
/* a word stepping through the band rows [y, y+7): in from below over 300 ms, out at the top the same way
   when the next one comes in (t_out = its t_in), so the two move together, 8 rows apart, and never overlap */
static void band_word(int y, const char *w, int t, int t_in, int t_out, const rgb_t *cols, int n)
{
    if (t < t_in || (t_out >= 0 && t >= t_out + 300)) return;
    int off = 8 - 8 * ease_out(prog(t, t_in, 300)) / 256;
    if (t_out >= 0 && t >= t_out) off = -8 * ease_out(prog(t, t_out, 300)) / 256;
    gfx_clip(0, y, 32, y + 7);
    text((32 - text_w(w)) / 2, y + off, w, cols, n);
    gfx_noclip();
}

/* big progress digits: 6 x 10 with a 2 px stroke, 2 px apart, centred on cx */
static const char *const DIGIT[10][10] = {
    { ".####.", "######", "##..##", "##..##", "##..##", "##..##", "##..##", "##..##", "######", ".####." },
    { "..##..", ".###..", "####..", "..##..", "..##..", "..##..", "..##..", "..##..", "######", "######" },
    { ".####.", "######", "##..##", "....##", "...###", "..###.", ".###..", "###...", "######", "######" },
    { ".####.", "######", "##..##", "....##", "..###.", "..####", "....##", "##..##", "######", ".####." },
    { "...###", "..####", ".##.##", "##..##", "##..##", "######", "######", "....##", "....##", "....##" },
    { "######", "######", "##....", "#####.", "######", "....##", "....##", "##..##", "######", ".####." },
    { ".####.", "######", "##....", "#####.", "######", "##..##", "##..##", "##..##", "######", ".####." },
    { "######", "######", "....##", "...###", "..###.", "..##..", ".###..", ".##...", ".##...", ".##..." },
    { ".####.", "######", "##..##", "##..##", ".####.", "######", "##..##", "##..##", "######", ".####." },
    { ".####.", "######", "##..##", "##..##", "######", ".#####", "....##", "....##", "######", ".####." },
};
static void digits(int cx, int y, int value, rgb_t c)
{
    char s[4];
    int n = snprintf(s, sizeof s, "%d", clampi(value, 0, 99)), x = cx - (n * 8 - 2) / 2;
    for (int i = 0; i < n; i++, x += 8)
        for (int r = 0; r < 10; r++) for (int k = 0; k < 6; k++) if (DIGIT[s[i] - '0'][r][k] == '#') gfx_px(x + k, y + r, c);
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
/* Bluetooth sign 7 x 11 (bit 6 = left); scale_y 0..256 squashes it towards its middle row (a flip) */
static const uint8_t BT7[11] = { 0x08, 0x0C, 0x0A, 0x49, 0x2A, 0x1C, 0x2A, 0x49, 0x0A, 0x0C, 0x08 };
static void rune(int cx, int cy, int scale_y, rgb_t c)
{
    for (int r = 0; r < 11; r++) {
        int y = cy + (r - 5) * scale_y / 256;
        for (int k = 0; k < 7; k++) if (BT7[r] & (0x40 >> k)) gfx_px(cx - 3 + k, y, c);
    }
}
/* the small one, 5 x 7 (bit 4 = left), for the small ring's hole */
static const uint8_t BT5[7] = { 0x04, 0x06, 0x15, 0x0E, 0x15, 0x06, 0x04 };
static void rune5(int cx, int cy, int scale_y, rgb_t c)
{
    for (int r = 0; r < 7; r++) {
        int y = cy + (r - 3) * scale_y / 256;
        for (int k = 0; k < 5; k++) if (BT5[r] & (0x10 >> k)) gfx_px(cx - 2 + k, y, c);
    }
}

/* =================================================================================================
 * The ring. Coordinates are doubled (pixel centres are odd numbers), so the ring sits exactly on the
 * 32 x 32 grid's centre. Big: outer radius 13.6 px, inner 8.3 px; small: 9.6 / 5.8 px. The gap is at the
 * top right (angles 210..238 of 256, 0 = right, clockwise); hues go round the ring and turn with `spin`.
 * ================================================================================================= */
#define BIG_RO 272                                      /* radii in 1/10 doubled px: 27.2 = 13.6 px */
#define BIG_RI 166
#define SMALL_RO 192
#define SMALL_RI 116
#define GAP_A 210u
#define GAP_B 238u
#define ARC   (256u - (GAP_B - GAP_A) - 1u)

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

typedef struct { int cy2, ro, ri; } ring_t;             /* centre row (doubled), radii (1/10 doubled px) */
static ring_t ring_at(int p)                            /* 0 = big and centred, 256 = small near the top */
{
    ring_t r = { lerp(32, 22, p), lerp(BIG_RO, SMALL_RO, p), lerp(BIG_RI, SMALL_RI, p) };
    return r;
}

/* brightness of each part of the ring: from = 0 right after the gap .. ARC-1 just before it */
typedef int (*ring_v_fn)(unsigned from, void *ctx);
static void ring(ring_t g, unsigned spin, ring_v_fn vfn, void *ctx, int whiten)
{
    int ro2 = g.ro * g.ro / 100, ri2 = g.ri * g.ri / 100, edge2 = (g.ro - 26) * (g.ro - 26) / 100;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int X = 2 * x + 1 - 32, Y = 2 * y + 1 - g.cy2, d2 = X * X + Y * Y;
            if (d2 <= ri2 || d2 > ro2) continue;
            unsigned a = angle8(X, Y);
            if (a >= GAP_A && a <= GAP_B) continue;
            unsigned from = (a - GAP_B - 1u) & 255u;
            int v = vfn(from, ctx);
            if (v <= 0) continue;
            int w = whiten;
            if (v > 255) { w += (v - 255); v = 255; }       /* > 255: a white-hot part */
            rgb_t c = gfx_hsv((uint8_t)(a + spin), 255, (uint8_t)(d2 > edge2 ? v * 3 / 4 : v));
            gfx_px(x, y, w ? mix(c, WHITE, w) : c);
        }
}
/* drawn clockwise from the gap up to `reveal` (0..256), the drawing tip glowing */
static int v_reveal(unsigned from, void *ctx)
{
    int reveal = *(int *)ctx, tip = reveal * (int)ARC / 256;
    if ((int)from >= tip) return 0;
    if (reveal < 256 && (int)from >= tip - 10) return 255 + 170 * (10 - (tip - (int)from)) / 10;
    return 255;
}

/* the "i": big (13 rows) or small (8 rows); grow 0..256 raises the stem from its foot, dot_y = the dot's
   offset (drops in from above) */
static void letter_i(int cy, int small, int grow, int dot_dy, rgb_t c)
{
    if (grow <= 0) return;
    if (!small) {
        int h = 10 * clampi(grow, 0, 256) / 256;        /* foot row cy+5, stem up from cy+4 */
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

/* the loose pixel on the 45-degree line through the gap; out 0..256: 0 = inside the ring's hole,
   256 = well outside; trail draws two fading copies behind it */
static void loose_pixel(ring_t g, int out, int trail, unsigned spin, int k)
{
    int r16 = g.ro * 8 / 10;                            /* outer radius in 1/16 px */
    int d0 = r16 * 70 / 100, d1 = r16 * 128 / 100;
    rgb_t base = gfx_hsv((uint8_t)(224u + spin), 50, 255);
    for (int s = trail ? 2 : 0; s >= 0; s--) {
        int d16 = lerp(d0, d1, out) - s * trail;         /* trail < 0: moving inwards, the copies outside */
        if (trail > 0 && d16 < d0 && s) continue;
        int off = d16 * 181 / 256;                      /* / sqrt 2 */
        int px = 16 + (off + 8) / 16 - 1, py = g.cy2 / 2 - (off + 8) / 16 - 1;
        gfx_rect(px, py, 2, 2, dim(base, s ? k * (3 - s) / 5 : k), 1);
    }
}

/* =================================================================================================
 * Boot, 3.5 s: the ring draws itself, the "i" rises and its dot drops in, the pixel escapes through the
 * gap; then the logo shrinks to the top and the name steps through the band below, then the version.
 * ================================================================================================= */
static const rgb_t NAME_INK[1] = { INK };
static void ring_hues(unsigned spin, rgb_t out5[5])    /* five colours taken round the ring, for "color" */
{
    for (int i = 0; i < 5; i++) out5[i] = gfx_hsv((uint8_t)(i * 48 + spin), 220, 255);
}
static void boot(int t)
{
    gfx_fill(0);
    unsigned spin = (unsigned)t / 6u;
    int shrink = ease_io(prog(t, 1350, 380));
    ring_t g = ring_at(shrink);
    int reveal = ease_out(prog(t, 0, 650));
    ring(g, spin, v_reveal, &reveal, 0);
    int grow = ease_out(prog(t, 330, 300));
    int dp = prog(t, 560, 320), dot = dp ? (-7 + 7 * back_out(dp) / 256) : -99;
    letter_i(g.cy2 / 2, shrink >= 128, grow, dot, INK);
    if (t >= 880) {                                     /* the pixel: a nudge back, then out through the gap */
        int out = t < 980 ? -20 * prog(t, 880, 100) / 256 : -20 + 276 * ease_out(prog(t, 980, 360)) / 256;
        loose_pixel(g, out, t < 1340 ? 22 : 0, spin, ease_out(prog(t, 880, 100)));
    }
    rgb_t hues[5];
    ring_hues(spin, hues);
    static const rgb_t grey[1] = { RGB(150, 156, 168) };
    band_word(24, "open", t, 1700, 2150, NAME_INK, 1);
    band_word(24, "ipixel", t, 2150, 2600, NAME_INK, 1);
    band_word(24, "color", t, 2600, 3050, hues, 5);
    band_word(24, "v101", t, 3050, -1, grey, 1);
}

/* =================================================================================================
 * First start (loops until something is chosen): the small logo, colours turning, the pixel bobbing in the
 * gap; "open ipixel color" scrolls through the band ("color" in the ring's colours), and between two
 * passes the "i" flips into the Bluetooth sign for a moment: connect to me.
 * ================================================================================================= */
static const char *const WELCOME = "open ipixel color";
#define WELCOME_MS_PX 32
#define WELCOME_BT_MS 1400
static int welcome_scroll(void) { return (text_w(WELCOME) + 32) * WELCOME_MS_PX; }
static int welcome_period(void) { return welcome_scroll() + WELCOME_BT_MS; }
static void small_i_squashed(int cy, int k)           /* k 0..256: the "i" squashed to its middle row (a flip) */
{
    if (k >= 256) { letter_i(cy, 1, 256, 0, INK); return; }
    int h = 4 * k / 256;
    gfx_clip(0, cy - h - 1, 32, cy + h + 1);
    letter_i(cy, 1, 256, 0, INK);
    gfx_noclip();
}
static void welcome(int t)
{
    gfx_fill(0);
    unsigned spin = (unsigned)t / 22u;
    ring_t g = ring_at(256);
    int full = 256, cy = g.cy2 / 2;
    ring(g, spin, v_reveal, &full, 0);
    int lt = t % welcome_period();
    if (lt < welcome_scroll()) {                        /* the name scrolls through the band */
        rgb_t cols[17], hues[5];
        ring_hues(spin, hues);
        for (int i = 0; i < 17; i++) cols[i] = i < 12 ? INK : hues[i - 12];
        gfx_clip(0, 24, 32, 31);
        text(32 - lt / WELCOME_MS_PX, 24, WELCOME, cols, 17);
        gfx_noclip();
        letter_i(cy, 1, 256, 0, INK);
    } else {                                            /* the "i" flips into the Bluetooth sign and back */
        int b = lt - welcome_scroll();
        if (b < 120) small_i_squashed(cy, 256 - 206 * prog(b, 0, 120) / 256);
        else if (b < WELCOME_BT_MS - 240) rune5(16, cy, b < 240 ? 50 + 206 * prog(b, 120, 120) / 256 : 256, BLUE);
        else if (b < WELCOME_BT_MS - 120) rune5(16, cy, 256 - 206 * prog(b, WELCOME_BT_MS - 240, 120) / 256, BLUE);
        else small_i_squashed(cy, 50 + 206 * prog(b, WELCOME_BT_MS - 120, 120) / 256);
    }
    loose_pixel(g, 150 + 100 * sinx(t * 256 / 1800) / 128, 0, spin, 255);
}

/* the logo itself, still: for the README banner, the web UI and the favicons (tools/make_brand.py) */
static void logo_still(void)
{
    gfx_fill(0);
    ring_t g = ring_at(0);
    int full = 256;
    ring(g, 0, v_reveal, &full, 0);
    letter_i(16, 0, 256, 0, INK);
    loose_pixel(g, 256, 0, 0, 255);
}

/* =================================================================================================
 * Firmware update: the big ring is the progress. It takes on its colours clockwise from the gap as the
 * firmware arrives (the block on its way pulses), pixels stream in through the gap, big digits in the
 * middle. Done: a light runs once round the ring, then a tick.
 * ================================================================================================= */
typedef struct { int filled, coming, pulse, sweep; } upd_t;
static int v_update(unsigned from, void *ctx)
{
    upd_t *u = ctx;
    int f = (int)from, v = f < u->filled ? 255 : f < u->coming ? u->pulse : 30;
    if (f < u->filled && f + 3 >= u->filled && u->filled < (int)ARC) v = 255 + 110;    /* the leading edge shines */
    if (u->sweep >= 0) { int dd = u->sweep - f; if (dd >= 0 && dd < 24) v = 255 + 230 * (24 - dd) / 24; }
    return v;
}
static void stream_in(ring_t g, int t, unsigned spin)  /* pixels flying in through the gap */
{
    for (int k = 0; k < 4; k++) {
        static const int side16[4] = { -20, 6, 22, -6 };
        int p = (t + k * 290) % 1160;
        int d16 = 352 - p * 208 / 1160;
        for (int tr = 0; tr < 2; tr++) {
            int dd = d16 + tr * 28;
            if (dd > 352) continue;
            int along = dd * 181 / 256, side = side16[k] * 181 / 256;
            int px = 16 + (along + side + 8) / 16 - 1, py = g.cy2 / 2 - (along - side + 8) / 16;
            rgb_t c = gfx_hsv((uint8_t)(224u + spin + k * 20u), tr ? 200 : 80, 255);
            gfx_px(px, py, tr ? dim(c, 90) : c);
        }
    }
}
static void update(int t, int pct, int done_ms)
{
    gfx_fill(0);
    unsigned spin = (unsigned)t / 16u;
    ring_t g = ring_at(0);
    upd_t u = { pct * (int)ARC / 100, clampi(pct + 26, 0, 100) * (int)ARC / 100, 80 + 150 * (sinx(t * 256 / 760) + 128) / 255, -1 };
    if (done_ms >= 0) { u.filled = u.coming = ARC; u.sweep = done_ms < 420 ? (int)ARC * done_ms / 380 : -1; }
    ring(g, spin, v_update, &u, 0);
    if (done_ms < 0) { stream_in(g, t, spin); digits(16, 11, pct, WHITE); }
    else if (done_ms >= 380) tick(10, 16, 14, 20, 22, 12, (done_ms - 380) / 18, GREEN);
}

/* =================================================================================================
 * Connected, 1.4 s: the loose pixel flies back in through the gap (something joined), a light ring
 * pulses out from the middle, and the "i" becomes the Bluetooth sign.
 * ================================================================================================= */
static int v_flash(unsigned from, void *ctx) { (void)from; return *(int *)ctx; }
static void connected(int t)
{
    gfx_fill(0);
    unsigned spin = (unsigned)t / 6u;
    ring_t g = ring_at(0);
    int hit = 360, flash = t >= hit && t < hit + 320 ? 255 + 200 * (256 - prog(t, hit, 320)) / 256 : 255;
    ring(g, spin, v_flash, &flash, 0);
    if (t < hit) {
        letter_i(16, 0, 256, 0, INK);
        int out = 256 - 560 * ease_in(prog(t, 40, hit - 40)) / 256;   /* from outside to the middle */
        loose_pixel(g, out, -26, spin, 255);
    } else {
        int r = lerp(10, 60, ease_out(prog(t, hit, 360)));          /* a light ring, 1/2 px */
        int k = 255 - 255 * prog(t, hit, 360) / 256;
        if (k > 0)
            for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
                int X = 2 * x + 1 - 32, Y = 2 * y + 1 - 32, d2 = X * X + Y * Y;
                if (d2 >= (r - 3) * (r - 3) && d2 <= r * r) gfx_px(x, y, mix(gfx_get(x, y), WHITE, k));
            }
        int pop = back_out(prog(t, hit, 200));
        rune(16, 16, clampi(pop, 0, 280), BLUE);
    }
    if (t > 1150) fade_all(255 - 255 * prog(t, 1150, 250) / 256);
}

/* =================================================================================================
 * Saving (an animation is being sent to the panel and stored): the small logo, its ring filling with colour
 * clockwise from the gap as the data arrives, a white arrow dropping into the ring again and again, the
 * percentage in the band ("saving" is a pixel too wide). Done: the arrow turns into a green tick and "saved" pushes in. (The big ring stays for
 * firmware updates; the small ring and a word are for everyday things.)
 * ================================================================================================= */
static void arrow_down(int cx, int y, rgb_t c)         /* 5 wide, 6 tall: a 1 px stem, a 3-row head */
{
    gfx_rect(cx, y, 1, 3, c, 1);
    gfx_rect(cx - 2, y + 3, 5, 1, c, 1);
    gfx_rect(cx - 1, y + 4, 3, 1, c, 1);
    gfx_px(cx, y + 5, c);
}
static void saving(int t, int pct, int done_ms)
{
    gfx_fill(0);
    unsigned spin = (unsigned)t / 16u;
    ring_t g = ring_at(256);
    int cy = g.cy2 / 2;
    upd_t u = { pct * (int)ARC / 100, clampi(pct + 18, 0, 100) * (int)ARC / 100, 80 + 150 * (sinx(t * 256 / 760) + 128) / 255, -1 };
    if (done_ms >= 0) { u.filled = u.coming = ARC; u.sweep = done_ms < 340 ? (int)ARC * done_ms / 300 : -1; }
    ring(g, spin, v_update, &u, 0);
    static const rgb_t ink[1] = { INK }, green[1] = { GREEN };
    if (done_ms < 0) {
        int p = t % 640, y = cy - 8 + 9 * ease_in(p * 256 / 640) / 256;      /* falls through the hole */
        int k = p < 120 ? 255 * p / 120 : p > 520 ? 255 * (640 - p) / 120 : 255;
        gfx_clip(10, cy - 5, 22, cy + 6);
        arrow_down(16, y, dim(INK, k));
        gfx_noclip();
        char s[6]; snprintf(s, sizeof s, "%d%%", pct);
        band_word(24, s, t, -300, -1, ink, 1);
    } else {
        if (done_ms >= 200) tick(13, cy, 15, cy + 2, 19, cy - 2, (done_ms - 200) / 25, GREEN);
        band_word(24, "100%", done_ms, -300, 200, ink, 1);
        band_word(24, "saved", done_ms, 200, -1, green, 1);
    }
}

/* ---- render ------------------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    out = fopen(argc > 1 ? argv[1] : "logos.bin", "wb");
    if (!out) return 1;
    gfx_target(&fb);
    logo_still(); snap("R", "logo");
    for (int t = 0; t < 3700; t += FRAME_MS) { boot(t); snap("R", "boot"); }
    for (int t = 0; t < welcome_period(); t += FRAME_MS) { welcome(t); snap("R", "welcome"); }
    for (int t = 0; t < 5000; t += FRAME_MS) { update(t, t * 100 / 5000, -1); snap("R", "update"); }
    for (int t = 0; t < 2000; t += FRAME_MS) { update(5000 + t, 100, t); snap("R", "update"); }
    for (int t = 0; t < 1400; t += FRAME_MS) { connected(t); snap("R", "bluetooth"); }
    for (int t = 0; t < 3200; t += FRAME_MS) { saving(t, t * 100 / 3200, -1); snap("R", "saving"); }
    for (int t = 0; t < 1600; t += FRAME_MS) { saving(3200 + t, 100, t); snap("R", "saving"); }
    fclose(out);
    return 0;
}
