/* Clock faces 10..25: words, Berlin clock, Fibonacci, morphing digits, hourglass, falling bricks, matrix
 * digits, fire digits, LCD watch, neon sign, dice, moon phase, orbit, progress, colour code, Roman numerals.
 * Their few bytes of state live in the upper half of the effects' shared memory (a clock face and an effect
 * never run at the same time); the matrix and fire faces run those effects, whose state is in the lower half. */
#include "clockface2.h"
#include "effects.h"
#include "fx_util.h"

typedef struct { int8_t cur, old; uint32_t t0; } dig_t;              /* a digit and when it last changed */
typedef struct { dig_t d[6]; } cstate_t;

_Static_assert(sizeof(cstate_t) <= FX_SCRATCH_BYTES / 2, "clock face state does not fit");
#define ST (*(cstate_t *)((uint8_t *)fx_scratch() + FX_SCRATCH_BYTES / 2))

static const char *const DOWN[7] = { "MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN" };
static const int DX4[4] = { 1, 8, 18, 25 };                           /* HH:MM digit columns */

static void track(int i, int v, uint32_t now)
{
    if (ST.d[i].cur != v) { ST.d[i].old = ST.d[i].cur; ST.d[i].cur = (int8_t)v; ST.d[i].t0 = now; }
}

static void tiny_c(int y, const char *s, rgb_t c) { gfx_tiny_text((32 - gfx_tiny_width(s)) / 2, y, s, c); }
static void text_c(int y, const char *s, rgb_t c) { gfx_text((32 - gfx_text_width(s)) / 2, y, s, c); }

static void two(char *b, unsigned v) { b[0] = (char)('0' + v / 10u % 10u); b[1] = (char)('0' + v % 10u); b[2] = 0; }

static void hhmm(char *b, int hh, int mm)                             /* "18:42" */
{
    two(b, (unsigned)hh); b[2] = ':'; two(b + 3, (unsigned)mm);
}

static void date_str(const clock_tm_t *tm, char *b)                  /* "SUN 22" */
{
    const char *d = DOWN[(tm->dow + 6) % 7];
    b[0] = d[0]; b[1] = d[1]; b[2] = d[2]; b[3] = ' '; two(b + 4, tm->day);
}

static int smooth(int p) { return p * p * (768 - 2 * p) >> 16; }     /* 0..256 -> 0..256, eased */

/* the 3 x 5 font, except W and M drawn 5 px wide (3 px wide they read as H); returns the width */
static const uint8_t WIDE_W[5] = { 0x11, 0x11, 0x15, 0x15, 0x0A }, WIDE_M[5] = { 0x11, 0x1B, 0x15, 0x11, 0x11 };

static int wtext(int x, int y, const char *s, rgb_t c, int draw)
{
    int x0 = x;
    for (; *s; s++) {
        const uint8_t *g = *s == 'W' ? WIDE_W : *s == 'M' ? WIDE_M : 0;
        if (g) {
            if (draw) for (int r = 0; r < 5; r++) for (int k = 0; k < 5; k++) if (g[r] & (16 >> k)) gfx_px(x + k, y + r, c);
            x += 6;
        } else {
            char one[2] = { *s, 0 };
            int w = gfx_tiny_width(one);
            if (draw) gfx_tiny_char(x, y, *s, c);
            x += (w ? w : 1) + 1;
        }
    }
    return x - x0 - 1;
}

static void wtext_c(int y, const char *s, rgb_t c) { wtext((32 - wtext(0, 0, s, c, 0)) / 2, y, s, c, 1); }

/* ---- 10: the time in words ------------------------------------------------------------------- */
static const char *const HOURW[12] = { "TWELVE", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE", "TEN", "ELEVEN" };

static void face_words(const clock_tm_t *tm, uint32_t now)
{
    static const char *const MW[7] = { "", "FIVE", "TEN", "QUARTER", "TWENTY", "TWENTY", "HALF" };
    const rgb_t dim = RGB(90, 110, 165), mw = RGB(225, 235, 255), hw = RGB(255, 185, 70);
    const char *ln[4]; rgb_t lc[4]; int n = 0;
    int m5 = tm->m / 5, h = tm->h % 12;
    if (m5 > 6) h = (h + 1) % 12;
    gfx_fill(0);
    if (m5 == 0) { ln[n] = "IT IS"; lc[n++] = dim; ln[n] = HOURW[h]; lc[n++] = hw; ln[n] = "O'CLOCK"; lc[n++] = dim; }
    else {
        int k = m5 <= 6 ? m5 : 12 - m5;
        ln[n] = MW[k]; lc[n++] = mw;
        if (k == 5) { ln[n] = "FIVE"; lc[n++] = mw; }
        ln[n] = m5 <= 6 ? "PAST" : "TO"; lc[n++] = dim;
        ln[n] = HOURW[h]; lc[n++] = hw;
    }
    int y = (30 - (n * 7 - 2)) / 2;
    for (int i = 0; i < n; i++) wtext_c(y + i * 7, ln[i], lc[i]);
    int extra = tm->m % 5;                                             /* minutes past the five */
    for (int i = 0; i < extra; i++)
        gfx_px(16 - extra * 2 + i * 4 + 1, 30, gfx_scale(RGB(255, 185, 70), (uint8_t)(170 + SN(now / 6 + i * 40) / 2)));
}

/* ---- 11: Berlin clock (set theory clock): rows of lamps --------------------------------------- */
static void face_berlin(const clock_tm_t *tm)
{
    const rgb_t R = RGB(255, 40, 25), Y = RGB(255, 190, 0), r = RGB(30, 5, 3), y = RGB(32, 22, 0);
    gfx_fill(0);
    gfx_circle(16, 3, 2, (tm->s & 1) ? y : Y, 1);
    for (int i = 0; i < 4; i++) {
        gfx_rect(1 + i * 8, 8, 6, 4, i < tm->h / 5 ? R : r, 1);
        gfx_rect(1 + i * 8, 14, 6, 4, i < tm->h % 5 ? R : r, 1);
        gfx_rect(1 + i * 8, 26, 6, 4, i < tm->m % 5 ? Y : y, 1);
    }
    for (int i = 0; i < 11; i++) {
        int quarter = i % 3 == 2;
        gfx_rect(i * 3, 20, 2, 4, i < tm->m / 5 ? (quarter ? R : Y) : (quarter ? r : y), 1);
    }
}

/* ---- 12: Fibonacci clock: squares of 1 1 2 3 5; red = hours, green = minutes / 5, blue = both ---- */
static const uint8_t FIB_V[5] = { 1, 1, 2, 3, 5 }, FIB_X[5] = { 2, 2, 0, 0, 3 }, FIB_Y[5] = { 0, 1, 0, 2, 0 };

static int fib_sum(int m)
{
    int sum = 0;
    for (int k = 0; k < 5; k++) if (m & (1 << k)) sum += FIB_V[k];
    return sum;
}

static int fib_count(int target)                                      /* ways to make target from the squares */
{
    int cnt = 0;
    for (int m = 0; m < 32; m++) cnt += fib_sum(m) == target;
    return cnt;
}

static int fib_pick(int target, int sel)                              /* the sel-th of those ways */
{
    for (int m = 0; m < 32; m++) if (fib_sum(m) == target && sel-- == 0) return m;
    return 0;
}

static void face_fibonacci(const clock_tm_t *tm)
{
    int h = tm->h % 12;
    if (!h) h = 12;
    int hn = fib_count(h), mn = fib_count(tm->m / 5);
    int hm = fib_pick(h, hn ? tm->m % hn : 0), mm = fib_pick(tm->m / 5, mn ? (tm->h + tm->m) % mn : 0);  /* a new pattern every minute */
    gfx_fill(0);
    for (int k = 0; k < 5; k++) {
        int ih = (hm >> k) & 1, im = (mm >> k) & 1;
        rgb_t c = ih && im ? RGB(50, 110, 255) : ih ? RGB(255, 50, 40) : im ? RGB(40, 210, 70) : RGB(28, 28, 32);
        gfx_rect(FIB_X[k] * 4, 5 + FIB_Y[k] * 4, FIB_V[k] * 4 - 1, FIB_V[k] * 4 - 1, c, 1);
    }
    for (int i = 0; i < 4; i++) gfx_rect(9 + i * 4, 27, 2, 2, i < tm->m % 5 ? RGB(230, 230, 230) : RGB(30, 30, 30), 1);
}

/* ---- 13: morphing digits: the pixels of a digit fly to make the next one ----------------------- */
static void morph_digit(int i, int x0, int y0, int w, int h, int th, uint32_t now, uint32_t dur, rgb_t col)
{
    dig_t *d = &ST.d[i];
    uint32_t el = now - d->t0;
    if (el >= dur || d->old == d->cur) { gfx_7seg(x0, y0, w, h, th, d->cur, col); return; }
    int8_t ox[80], oy[80], nx[80], ny[80];
    int no = 0, nn = 0;
    for (int py = 0; py < h; py++)
        for (int px = 0; px < w; px++) {
            if (d->old >= 0 && no < 80 && gfx_7seg_hit(px, py, w, h, th, d->old)) { ox[no] = (int8_t)px; oy[no++] = (int8_t)py; }
            if (nn < 80 && gfx_7seg_hit(px, py, w, h, th, d->cur)) { nx[nn] = (int8_t)px; ny[nn++] = (int8_t)py; }
        }
    int p = (int)(el * 256u / dur), e = smooth(p);
    for (int k = 0; k < no; k++) gfx_px(x0 + ox[k], y0 + oy[k], gfx_scale(col, (uint8_t)((256 - e) * 3 / 4)));
    rgb_t fly = fx_mix(col, RGB(255, 255, 255), SN(p / 2) > 0 ? SN(p / 2) * 2 : 0);
    for (int k = 0; k < nn; k++) {
        int sx = no ? ox[k * no / nn] : w / 2, sy = no ? oy[k * no / nn] : h / 2;
        int x = sx * 256 + (nx[k] - sx) * e, y = sy * 256 + (ny[k] - sy) * e;
        gfx_px(x0 + ((x + 128) >> 8), y0 + ((y + 128) >> 8), fly);
    }
}

static void face_morph(const clock_tm_t *tm, int hh, int show_date, uint32_t now, unsigned ms)
{
    int v[6] = { hh / 10, hh % 10, tm->m / 10, tm->m % 10, tm->s / 10, tm->s % 10 };
    for (int i = 0; i < 6; i++) track(i, v[i], now);
    gfx_fill(0);
    for (int i = 0; i < 4; i++) morph_digit(i, DX4[i], 2, 6, 11, 2, now, 700, i < 2 ? RGB(60, 200, 255) : RGB(255, 90, 200));
    if (ms < 500) { gfx_rect(15, 4, 2, 2, RGB(200, 200, 220), 1); gfx_rect(15, 9, 2, 2, RGB(200, 200, 220), 1); }
    morph_digit(4, 10, 16, 5, 9, 1, now, 400, RGB(255, 210, 60));
    morph_digit(5, 17, 16, 5, 9, 1, now, 400, RGB(255, 210, 60));
    if (show_date) { char b[8]; date_str(tm, b); tiny_c(27, b, RGB(120, 140, 190)); }
}

/* ---- 14: hourglass: the sand of this hour runs down; hours and minutes beside it --------------- */
static void face_hourglass(const clock_tm_t *tm, int hh, int show_date, uint32_t now)
{
    const int cx = 10;
    const rgb_t wood = RGB(120, 70, 30), glass = RGB(45, 65, 100), sand = RGB(235, 180, 80);
    gfx_fill(0);
    gfx_rect(2, 1, 17, 2, wood, 1); gfx_rect(2, 29, 17, 2, wood, 1);
    int hw[32], capt = 0, capb = 0;
    for (int y = 3; y <= 28; y++) {
        hw[y] = y <= 15 ? 1 + (15 - y) * 6 / 12 : 1 + (y - 16) * 6 / 12;
        if (y <= 15) capt += 2 * hw[y] - 1; else capb += 2 * hw[y] - 1;
    }
    int down = tm->m * 60 + tm->s;                                      /* seconds of this hour run through */
    int gb = down * capb / 3600, gt = (3599 - down) * capt / 3600, surface = 29;
    for (int y = 28; y >= 16 && gb > 0; y--) {                         /* the pile below */
        int w = 2 * hw[y] - 1, k = gb < w ? gb : w;
        for (int i = 0; i < k; i++) gfx_px(cx - k / 2 + i, y, sand);
        gb -= k; surface = y;
    }
    for (int y = 15; y >= 3 && gt > 0; y--) {                          /* what is left above */
        int w = 2 * hw[y] - 1, k = gt < w ? gt : w;
        for (int i = 0; i < k; i++) gfx_px(cx - k / 2 + i, y, sand);
        gt -= k;
    }
    if (down < 3599)                                                   /* the falling stream */
        for (int y = 16; y < surface; y++) if (((y * 3 + (int)(now / 25)) & 3) != 0) gfx_px(cx, y, sand);
    for (int y = 3; y <= 28; y++) { gfx_px(cx - hw[y], y, glass); gfx_px(cx + hw[y], y, glass); }
    char b[4];
    two(b, (unsigned)hh); gfx_text(21, 5, b, RGB(230, 230, 240));
    two(b, tm->m); gfx_text(21, 14, b, RGB(235, 180, 80));
    if (show_date) { gfx_tiny_text(21, 24, DOWN[(tm->dow + 6) % 7], RGB(110, 130, 180)); }
}

/* ---- 15: falling bricks: a digit that changes falls away and is rebuilt brick by brick --------- */
static void brick(int x, int y, uint8_t hue)
{
    gfx_px(x, y, gfx_hsv(hue, 120, 255)); gfx_px(x + 1, y, gfx_hsv(hue, 160, 230));
    gfx_rect(x, y + 1, 2, 2, gfx_hsv(hue, 230, 210), 1);
}

static void brick_digit(int i, int x0, int y0, uint8_t hue, uint32_t now)
{
    dig_t *d = &ST.d[i];
    uint32_t el = now - d->t0;
    int lead = 0;
    if (d->old >= 0) {
        lead = 420;
        if (el < 450) {                                                 /* the old one falls away */
            uint16_t g = gfx_tiny_glyph((char)('0' + d->old));
            int fall = (int)(el * el / 2600u);
            for (int r = 0; r < 5; r++)
                for (int c = 0; c < 3; c++)
                    if (g & (1u << (14 - r * 3 - c))) brick(x0 + c * 2, y0 + r * 3 + fall, hue);
        }
    }
    if (d->cur < 0) return;
    uint16_t g = gfx_tiny_glyph((char)('0' + d->cur));
    int k = 0;
    for (int r = 4; r >= 0; r--)                                       /* bottom row first */
        for (int c = 0; c < 3; c++) {
            if (!(g & (1u << (14 - r * 3 - c)))) continue;
            uint32_t start = (uint32_t)(lead + k++ * 45);
            if (el < start) continue;
            uint32_t dt = el - start;
            int ty = y0 + r * 3, y = -3 + (int)(dt * dt / 1500u);
            brick(x0 + c * 2, y > ty ? ty : y, hue);
        }
}

static void face_bricks(const clock_tm_t *tm, int hh, int show_date, uint32_t now, unsigned ms)
{
    static const uint8_t HUE[4] = { 0, 28, 140, 165 };
    int v[4] = { hh / 10, hh % 10, tm->m / 10, tm->m % 10 };
    for (int i = 0; i < 4; i++) track(i, v[i], now);
    gfx_fill(0);
    for (int i = 0; i < 4; i++) brick_digit(i, DX4[i], 4, HUE[i], now);
    if (ms < 500) { gfx_rect(15, 8, 2, 2, RGB(200, 200, 200), 1); gfx_rect(15, 14, 2, 2, RGB(200, 200, 200), 1); }
    if (show_date) { char b[8]; date_str(tm, b); tiny_c(23, b, RGB(150, 150, 170)); }
    gfx_rect(1, 30, (tm->s * 30 + 30) / 60, 1, RGB(60, 60, 90), 1);
}

/* ---- 16 and 17: digits over matrix rain, digits made of fire ----------------------------------- */
static int digits_hit(int x, int y, const int *v, int y0, int w, int h, int th)
{
    for (int i = 0; i < 4; i++) if (gfx_7seg_hit(x - DX4[i], y - y0, w, h, th, v[i])) return 1;
    return 0;
}

static void face_matrix(const clock_tm_t *tm, int hh, int show_date, uint32_t now, unsigned ms)
{
    int v[4] = { hh / 10, hh % 10, tm->m / 10, tm->m % 10 };
    effects[2].frame(now);
    for (int y = 7; y < 21; y++)                                       /* dark outline, then the digits */
        for (int x = 0; x < 32; x++) {
            if (digits_hit(x, y, v, 8, 6, 11, 2)) gfx_px(x, y, RGB(160, 255, 175));
            else if (digits_hit(x + 1, y, v, 8, 6, 11, 2) || digits_hit(x - 1, y, v, 8, 6, 11, 2) ||
                     digits_hit(x, y + 1, v, 8, 6, 11, 2) || digits_hit(x, y - 1, v, 8, 6, 11, 2)) gfx_px(x, y, 0);
        }
    if (ms < 500) { gfx_rect(15, 10, 2, 2, RGB(160, 255, 175), 1); gfx_rect(15, 15, 2, 2, RGB(160, 255, 175), 1); }
    if (show_date) { char b[8]; date_str(tm, b); gfx_rect(4, 24, 24, 7, 0, 1); tiny_c(25, b, RGB(90, 230, 110)); }
}

static void face_fire(const clock_tm_t *tm, int hh, int show_date, uint32_t now, unsigned ms)
{
    int v[4] = { hh / 10, hh % 10, tm->m / 10, tm->m % 10 };
    effects[1].frame(now);
    fb_t *f = gfx_fb();
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            uint8_t *p = (*f)[y][x];
            int in = digits_hit(x, y, v, 13, 6, 17, 2) || (ms < 500 && x >= 15 && x <= 16 && (y == 17 || y == 18 || y == 24 || y == 25));
            if (in) { if (p[0] < 90) { p[0] = 90; p[1] = 14; p[2] = 0; } }
            else { p[0] = (uint8_t)(p[0] * 26 / 256); p[1] = (uint8_t)(p[1] * 20 / 256); p[2] = (uint8_t)(p[2] * 20 / 256); }
        }
    if (show_date) { char b[8]; date_str(tm, b); tiny_c(3, b, RGB(255, 140, 40)); }
}

/* ---- 18: LCD watch: dark segments on a lit screen, unlit segments faintly visible -------------- */
static void face_lcd(const clock_tm_t *tm, int hh, int fmt24, int show_date, unsigned ms)
{
    static const int LX[4] = { 1, 8, 18, 25 };
    const rgb_t bg = RGB(44, 54, 34), ghost = RGB(36, 45, 28), ink = RGB(4, 9, 4);
    gfx_fill(bg);
    gfx_rect(0, 0, 32, 32, RGB(12, 14, 10), 0);
    int v[4] = { hh / 10, hh % 10, tm->m / 10, tm->m % 10 };
    if (!fmt24 && v[0] == 0) v[0] = 10;                                 /* 12 hour: no leading zero */
    for (int i = 0; i < 4; i++) { gfx_7seg(LX[i], 10, 6, 11, 2, 8, ghost); gfx_7seg(LX[i], 10, 6, 11, 2, v[i], ink); }
    gfx_rect(15, 12, 2, 2, ms < 500 ? ink : ghost, 1); gfx_rect(15, 17, 2, 2, ms < 500 ? ink : ghost, 1);
    gfx_7seg(19, 23, 4, 7, 1, 8, ghost); gfx_7seg(19, 23, 4, 7, 1, tm->s / 10, ink);
    gfx_7seg(25, 23, 4, 7, 1, 8, ghost); gfx_7seg(25, 23, 4, 7, 1, tm->s % 10, ink);
    if (show_date) {
        char b[3];
        gfx_tiny_text(2, 3, DOWN[(tm->dow + 6) % 7], ink);
        two(b, tm->day); gfx_tiny_text(30 - gfx_tiny_width(b), 3, b, ink);
    }
    if (!fmt24) gfx_tiny_text(3, 25, tm->h < 12 ? "AM" : "PM", ink);
}

/* ---- 19: neon sign: glowing tubes on a brick wall, the odd flicker ------------------------------ */
static void neon_digit(int x0, int y0, int d, rgb_t core, rgb_t glow)   /* 5 x 13 tubes, glow around */
{
    for (int py = -1; py <= 13; py++)
        for (int px = -1; px <= 5; px++) {
            if (gfx_7seg_hit(px, py, 5, 13, 1, d)) continue;
            int n = gfx_7seg_hit(px + 1, py, 5, 13, 1, d) + gfx_7seg_hit(px - 1, py, 5, 13, 1, d) +
                    gfx_7seg_hit(px, py + 1, 5, 13, 1, d) + gfx_7seg_hit(px, py - 1, 5, 13, 1, d);
            if (n) gfx_px_add(x0 + px, y0 + py, gfx_scale(glow, (uint8_t)(n > 1 ? 255 : 170)));
        }
    gfx_7seg(x0, y0, 5, 13, 1, d, core);
}

static void face_neon(const clock_tm_t *tm, int hh, int show_date, uint32_t now, unsigned ms)
{
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int mortar = (y % 4 == 3) || ((x + ((y / 4) & 1) * 3) % 6 == 5);
            gfx_px(x, y, mortar ? RGB(6, 3, 6) : RGB(15, 7, 11));
        }
    int v[4] = { hh / 10, hh % 10, tm->m / 10, tm->m % 10 };
    for (int i = 0; i < 4; i++) {
        int dim = (fx_hash(now / 90 + (uint32_t)i * 77u) & 63u) == 0;    /* a tube flickers now and then */
        rgb_t core = i < 2 ? RGB(255, 160, 225) : RGB(160, 250, 255), glow = i < 2 ? RGB(110, 0, 70) : RGB(0, 70, 95);
        if (dim) { core = gfx_scale(core, 90); glow = gfx_scale(glow, 90); }
        static const int NX[4] = { 2, 9, 18, 25 };                      /* 2 px apart, room for the glow */
        neon_digit(NX[i], 4, v[i], core, glow);
    }
    if (ms < 500) { gfx_px(15, 7, RGB(255, 160, 225)); gfx_px(16, 7, RGB(255, 160, 225)); gfx_px(15, 13, RGB(160, 250, 255)); gfx_px(16, 13, RGB(160, 250, 255)); }
    if (show_date) {
        char b[8]; date_str(tm, b);
        int x = (32 - gfx_tiny_width(b)) / 2;
        gfx_tiny_text(x, 23, b, RGB(255, 230, 120));
    } else {
        int len = (tm->s * 28 + 30) / 60;
        gfx_rect(2, 23, len, 1, RGB(210, 120, 255), 1);
        gfx_rect(2, 22, len, 1, RGB(50, 10, 70), 1); gfx_rect(2, 24, len, 1, RGB(50, 10, 70), 1);
    }
}

/* ---- 20: dice: the pips show the digits (7, 8 and 9 use the full 3 x 3 grid) -------------------- */
static const uint16_t PIPS[10] = { 0x000, 0x010, 0x101, 0x111, 0x145, 0x155, 0x16D, 0x17D, 0x1EF, 0x1FF };

static void die(int x0, int y0, uint16_t pips, rgb_t body)
{
    gfx_rect(x0, y0, 14, 14, body, 1);
    gfx_px(x0, y0, 0); gfx_px(x0 + 13, y0, 0); gfx_px(x0, y0 + 13, 0); gfx_px(x0 + 13, y0 + 13, 0);
    for (int k = 0; k < 9; k++)
        if (pips & (1u << (8 - k))) gfx_rect(x0 + 2 + (k % 3) * 4, y0 + 2 + (k / 3) * 4, 2, 2, RGB(255, 255, 255), 1);
}

static void face_dice(const clock_tm_t *tm, int hh, uint32_t now, unsigned ms)
{
    int v[4] = { hh / 10, hh % 10, tm->m / 10, tm->m % 10 };
    gfx_fill(0);
    for (int i = 0; i < 4; i++) {
        track(i, v[i], now);
        uint32_t el = now - ST.d[i].t0;
        int x = (i & 1) ? 17 : 1, y = (i & 2) ? 17 : 1;
        uint16_t p = PIPS[v[i]];
        if (el < 650) {                                                 /* rolling */
            uint32_t h = fx_hash(now / 70 + (uint32_t)i * 31u);
            p = PIPS[1 + h % 6u];
            x += (int)(h >> 8 & 2) - 1; y += (int)(h >> 9 & 2) - 1;
        }
        die(x, y, p, i < 2 ? RGB(150, 25, 35) : RGB(25, 60, 170));
    }
    if (ms < 500) gfx_rect(15, 15, 2, 2, RGB(200, 200, 200), 1);
}

/* ---- 21: moon phase (from the date) over the time ------------------------------------------------ */
static int32_t days_from_civil(int y, unsigned m, unsigned d)          /* days since 1970-01-01 */
{
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (m > 2 ? m - 3 : m + 9) + 2u) / 5u + d - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

static void face_moon(const clock_tm_t *tm, int hh, int show_date, uint32_t now)
{
    static const int8_t CRATER[4][3] = { { -5, -4, 3 }, { 4, 5, 2 }, { 6, -7, 2 }, { -2, 8, 2 } };   /* half px */
    int32_t days = days_from_civil(tm->year, tm->mon, tm->day) - 10962;   /* since the new moon of 2000-01-06 */
    int32_t age = (days * 100 + tm->h * 100 / 24 + tm->m * 100 / 1440 - 76) % 2953;
    if (age < 0) age += 2953;
    uint8_t ph = (uint8_t)(age * 256 / 2953);                           /* 0 new, 128 full */
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            uint32_t h = fx_hash((uint32_t)(y * 32 + x) * 5u);
            gfx_px(x, y, (h >> 27) == 0 ? gfx_scale(RGB(170, 170, 210), sin8[(uint8_t)(now / 12 + (h >> 8))] / 3) : 0);
        }
    for (int y = 3; y <= 19; y++)                                       /* radius 8, centre (15.5, 11) */
        for (int x = 7; x <= 24; x++) {
            int dx = x * 2 - 31, dy = (y - 11) * 2;
            if (dx * dx + dy * dy > 16 * 16) continue;
            int w = (int)fx_isqrt((unsigned)(16 * 16 - dy * dy)), xt = w * CS(ph) / 128;
            int lit = ph < 128 ? dx > xt : dx < -xt;
            rgb_t c = RGB(16, 18, 30);
            if (lit) {
                c = RGB(235, 230, 205);
                for (int k = 0; k < 4; k++) {
                    int cx = dx - CRATER[k][0] * 2, cy = dy - CRATER[k][1] * 2;
                    if (cx * cx + cy * cy <= CRATER[k][2] * CRATER[k][2] * 4) c = RGB(180, 175, 155);
                }
            }
            gfx_px(x, y, c);
        }
    char b[6];
    hhmm(b, hh, tm->m);
    text_c(23, b, RGB(200, 210, 255));
    if (show_date) {
        char d[3]; two(d, tm->day);
        gfx_tiny_text(1, 1, d, RGB(120, 130, 170));
        gfx_tiny_text(31 - gfx_tiny_width(DOWN[(tm->dow + 6) % 7]), 1, DOWN[(tm->dow + 6) % 7], RGB(120, 130, 170));
    }
}

/* ---- 22: orbit: the hour is a planet round the sun, the minute its moon, the second a comet ----- */
static void face_orbit(const clock_tm_t *tm, uint32_t now, unsigned ms)
{
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) gfx_px(x, y, (fx_hash((uint32_t)(y * 32 + x) * 3u) >> 27) == 0 ? RGB(25, 25, 40) : 0);
    for (int i = 0; i < 12; i++) {
        uint8_t a = (uint8_t)(i * 256 / 12);
        gfx_px(16 + SN(a) * 15 / 128, 16 - CS(a) * 15 / 128, i % 3 ? RGB(70, 70, 100) : RGB(170, 170, 220));
    }
    for (int a = 0; a < 256; a += 6) gfx_px_add(16 + SN(a) * 11 / 128, 16 - CS(a) * 11 / 128, RGB(20, 40, 70));   /* the planet's orbit */
    for (int y = 12; y <= 20; y++)                                     /* the sun's glow */
        for (int x = 12; x <= 20; x++) {
            int d2 = (x - 16) * (x - 16) + (y - 16) * (y - 16);
            if (d2 < 20) gfx_px_add(x, y, gfx_scale(RGB(255, 140, 20), (uint8_t)((20 - d2) * 6)));
        }
    for (int k = 0; k < 8; k++) {                                       /* the comet and its tail */
        uint32_t sm = (uint32_t)tm->s * 1000u + ms;
        uint8_t a = (uint8_t)((sm + 60000u - (uint32_t)k * 250u) % 60000u * 256u / 60000u);
        gfx_px_add(16 + SN(a) * 14 / 128, 16 - CS(a) * 14 / 128, gfx_scale(RGB(255, 90, 60), (uint8_t)(255 - k * 30)));
    }
    gfx_circle(16, 16, 2, RGB(255, 200, 40), 1);
    gfx_px(16, 16, RGB(255, 255, 220));
    uint8_t ha = (uint8_t)((tm->h % 12) * 256 / 12 + tm->m * 256 / 720);
    int px = 16 + SN(ha) * 11 / 128, py = 16 - CS(ha) * 11 / 128;
    gfx_circle(px, py, 1, RGB(60, 170, 255), 1);
    gfx_px(px, py, RGB(150, 220, 255));
    uint8_t ma = (uint8_t)(tm->m * 256 / 60 + tm->s * 256 / 3600);
    for (int a = 0; a < 256; a += 32) gfx_px_add(px + SN(a) * 4 / 128, py - CS(a) * 4 / 128, RGB(25, 25, 30));
    gfx_px(px + SN(ma) * 4 / 128, py - CS(ma) * 4 / 128, RGB(240, 240, 240));
    (void)now;
}

/* ---- 23: progress: how much of the day, week, month and year has gone ------------------------- */
static unsigned month_days(unsigned y, unsigned m)
{
    static const uint8_t D[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return (m == 2 && (y % 4u) == 0) ? 29u : D[m - 1];
}

static void face_progress(const clock_tm_t *tm, int hh, int show_date)
{
    static const char *const LB[4] = { "D", "W", "M", "Y" };
    static const rgb_t COL[4] = { RGB(255, 200, 60), RGB(90, 230, 110), RGB(80, 190, 255), RGB(230, 100, 255) };
    uint32_t sod = tm->h * 3600u + tm->m * 60u + tm->s, doy = 0;
    for (unsigned m = 1; m < tm->mon; m++) doy += month_days(tm->year, m);
    doy += tm->day - 1u;
    unsigned yd = (tm->year % 4u) == 0 ? 366u : 365u, md = month_days(tm->year, tm->mon);
    unsigned f[4] = {
        sod * 10u / 864u,
        ((tm->dow - 1u) * 86400u + sod) * 10u / 6048u,
        ((tm->day - 1u) * 86400u + sod) / (md * 86u),
        (doy * 86400u + sod) / (yd * 86u),
    };
    gfx_fill(0);
    char b[6];
    hhmm(b, hh, tm->m);
    gfx_tiny_text(1, 1, b, RGB(230, 230, 240));
    if (show_date) { char d[3]; two(d, tm->day); gfx_tiny_text(31 - gfx_tiny_width(d), 1, d, RGB(130, 140, 180)); }
    for (int i = 0; i < 4; i++) {
        unsigned v = f[i] > 1000u ? 1000u : f[i], fill = v * 25u, full = fill / 1000u;
        int y = 8 + i * 6;
        wtext(0, y, LB[i], gfx_scale(COL[i], 200), 1);
        gfx_rect(7, y + 1, 25, 3, gfx_scale(COL[i], 30), 1);
        gfx_rect(7, y + 1, (int)full, 3, COL[i], 1);
        if (full < 25) gfx_rect(7 + (int)full, y + 1, 1, 3, gfx_scale(COL[i], (uint8_t)(30 + (fill % 1000u) * 225u / 1000u)), 1);
    }
}

/* ---- 24: colour code: the time as a colour (hour = red, minute = green, second = blue) ---------- */
static void face_colour(const clock_tm_t *tm, int hh, int show_date)
{
    static const char HEX[] = "0123456789ABCDEF";
    unsigned r = tm->h * 255u / 23u, g = tm->m * 255u / 59u, b = tm->s * 255u / 59u;
    gfx_fill(RGB(r * 3 / 5, g * 3 / 5, b * 3 / 5));
    unsigned lum = (r * 3u + g * 6u + b) / 10u * 3u / 5u;
    rgb_t ink = lum > 105 ? RGB(10, 10, 10) : RGB(255, 255, 255);
    char t[6], h[7] = { HEX[r >> 4], HEX[r & 15], HEX[g >> 4], HEX[g & 15], HEX[b >> 4], HEX[b & 15], 0 };
    hhmm(t, hh, tm->m);
    text_c(6, t, ink);
    tiny_c(17, h, ink);
    if (show_date) { char d[8]; date_str(tm, d); tiny_c(25, d, ink); }
}

/* ---- 25: Roman numerals ------------------------------------------------------------------------- */
static void roman(unsigned n, char *b)
{
    static const uint16_t V[9] = { 100, 90, 50, 40, 10, 9, 5, 4, 1 };
    static const char *const S[9] = { "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I" };
    if (!n) { b[0] = 'N'; b[1] = 0; return; }                          /* nulla: no zero in Roman numerals */
    for (int i = 0; i < 9; i++)
        while (n >= V[i]) { for (const char *s = S[i]; *s; s++) *b++ = *s; n -= V[i]; }
    *b = 0;
}

static void face_roman(const clock_tm_t *tm, int hh, int show_date)
{
    char b[12];
    gfx_fill(0);
    roman((unsigned)hh, b); text_c(3, b, RGB(255, 190, 60));
    gfx_rect(7, 12, 18, 1, RGB(90, 60, 15), 1); gfx_px(16, 12, RGB(255, 190, 60)); gfx_px(15, 12, RGB(255, 190, 60));
    roman(tm->m, b); tiny_c(15, b, RGB(235, 225, 210));
    roman(tm->s, b); tiny_c(21, b, RGB(120, 100, 80));
    if (show_date) {
        char d[12], m[8];
        roman(tm->day, d); roman(tm->mon, m);
        int w = gfx_tiny_width(d) + 3 + gfx_tiny_width(m), x = (32 - w) / 2;
        x = gfx_tiny_text(x, 27, d, RGB(150, 120, 70)) + 2;
        gfx_tiny_text(x, 27, m, RGB(150, 120, 70));
    }
}

/* ---- dispatch ----------------------------------------------------------------------------------- */
static uint8_t  last_style = 255;
static uint32_t last_now;

void clock2_draw(unsigned style, const clock_tm_t *tm, int hh, int fmt24, int show_date, uint32_t now, unsigned ms)
{
    style %= CLOCK2_STYLES;
    if (style != last_style || now - last_now > 250u) {               /* (re)started: fresh state */
        for (int i = 0; i < 6; i++) { ST.d[i].cur = -1; ST.d[i].old = -1; ST.d[i].t0 = now; }
        if (style == 6) effects[2].init();
        if (style == 7) effects[1].init();
    }
    last_style = (uint8_t)style; last_now = now;
    switch (style) {
    case 0:  face_words(tm, now); break;
    case 1:  face_berlin(tm); break;
    case 2:  face_fibonacci(tm); break;
    case 3:  face_morph(tm, hh, show_date, now, ms); break;
    case 4:  face_hourglass(tm, hh, show_date, now); break;
    case 5:  face_bricks(tm, hh, show_date, now, ms); break;
    case 6:  face_matrix(tm, hh, show_date, now, ms); break;
    case 7:  face_fire(tm, hh, show_date, now, ms); break;
    case 8:  face_lcd(tm, hh, fmt24, show_date, ms); break;
    case 9:  face_neon(tm, hh, show_date, now, ms); break;
    case 10: face_dice(tm, hh, now, ms); break;
    case 11: face_moon(tm, hh, show_date, now); break;
    case 12: face_orbit(tm, now, ms); break;
    case 13: face_progress(tm, hh, show_date); break;
    case 14: face_colour(tm, hh, show_date); break;
    default: face_roman(tm, hh, show_date); break;
    }
}
