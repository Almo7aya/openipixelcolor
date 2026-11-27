/* Clock faces 26..42: falling blocks, count the squares (TIX style), abacus, oscilloscope, clock of clocks,
 * station clock, cuckoo clock, VFD, flip-dot, terminal, odometer, candle, decimal time, radar, ticker,
 * melting clock, 3D digits. As in clockface2.c, their few bytes of state live in the upper half of the
 * effects' shared memory (a clock face and an effect never run at the same time), and a face starts fresh
 * whenever it is picked. The faces that build themselves up (blocks, clock of clocks, flip-dot, terminal,
 * odometer, cuckoo) wait a moment first, so the start can be seen. */
#include "clockface3.h"
#include "effects.h"
#include "fx_util.h"

typedef struct {
    int8_t   cur[8], old[8];   /* tracked values (mostly digits) and the ones before */
    uint32_t t[8];             /* when each one last changed */
    int8_t   from[4];          /* clock of clocks: the digits the hands turn away from */
    uint32_t ev;               /* when the face's own event starts: a turn of the clocks, typing, a cuckoo call */
    uint8_t  calls;            /* cuckoo calls in this event */
} c3_t;

_Static_assert(sizeof(c3_t) <= FX_SCRATCH_BYTES / 2, "clock face state does not fit");
#define C3 (*(c3_t *)((uint8_t *)fx_scratch() + FX_SCRATCH_BYTES / 2))

#define START_WAIT 900u                                                /* ms before a face builds itself up */

static const char *const DOW[7] = { "MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN" };
static const char *const MON[12] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
static const int DX4[4] = { 1, 8, 18, 25 };                           /* HH:MM digit columns */

static void two(char *b, unsigned v) { b[0] = (char)('0' + v / 10u % 10u); b[1] = (char)('0' + v % 10u); b[2] = 0; }
static void hhmm(char *b, int hh, int mm) { two(b, (unsigned)hh); b[2] = ':'; two(b + 3, (unsigned)mm); }

static void date_str(const clock_tm_t *tm, char *b)                  /* "SUN 22" */
{
    const char *d = DOW[(tm->dow + 6) % 7];
    b[0] = d[0]; b[1] = d[1]; b[2] = d[2]; b[3] = ' '; two(b + 4, tm->day);
}

static void tiny_c(int y, const char *s, rgb_t c) { gfx_tiny_text((32 - gfx_tiny_width(s)) / 2, y, s, c); }

static int ease(int p)                                                /* 0..256 -> 0..256, eased in and out */
{
    if (p <= 0) return 0;
    if (p >= 256) return 256;
    return p * p * (768 - 2 * p) >> 16;
}

static int track(int i, int v, uint32_t now)                         /* 1 when value i changed */
{
    if (C3.cur[i] == v) return 0;
    C3.old[i] = C3.cur[i]; C3.cur[i] = (int8_t)v; C3.t[i] = now;
    return 1;
}

static void glyph57(int x, int y, char ch, rgb_t c)                   /* a 5 x 7 character, all 5 columns */
{
    const uint8_t *g = font5x7[ch - 32];
    for (int col = 0; col < 5; col++)
        for (int r = 0; r < 7; r++)
            if (g[col] & (1u << r)) gfx_px(x + col, y + r, c);
}

/* a hand, bar or dot from (cx, cy) (1/16 px) at angle a (0 = up, clockwise): from r0 to r1 (1/16 px), half
   width hw (1/16 px), with a soft one pixel edge; r0 == r1 draws a dot */
static void aa_hand(int cx, int cy, int a, int r0, int r1, int hw, rgb_t c)
{
    int ux = SN(a), uy = -CS(a);
    int ax = cx + ux * r0 / 127, ay = cy + uy * r0 / 127, bx = cx + ux * r1 / 127, by = cy + uy * r1 / 127;
    int vx = bx - ax, vy = by - ay, len2 = vx * vx + vy * vy, m = hw + 16;
    int x0 = ((ax < bx ? ax : bx) - m) >> 4, x1 = ((ax > bx ? ax : bx) + m) >> 4;
    int y0 = ((ay < by ? ay : by) - m) >> 4, y1 = ((ay > by ? ay : by) + m) >> 4;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > 31) x1 = 31;
    if (y1 > 31) y1 = 31;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            int px = x * 16 + 8 - ax, py = y * 16 + 8 - ay, t = px * vx + py * vy;
            if (len2 && t > 0) {
                if (t >= len2) { px -= vx; py -= vy; }
                else { px -= vx * t / len2; py -= vy * t / len2; }
            }
            int k = (hw + 8 - (int)fx_isqrt((unsigned)(px * px + py * py))) * 16;
            if (k > 0) gfx_px(x, y, k >= 256 ? c : fx_mix(gfx_get(x, y), c, k));
        }
}

/* digits drawn as thin tubes with a glow round them (oscilloscope, VFD) */
static void glow_digit(int x0, int y0, int w, int h, int th, int d, rgb_t core, rgb_t glow)
{
    for (int py = -1; py <= h; py++)
        for (int px = -1; px <= w; px++) {
            if (gfx_7seg_hit(px, py, w, h, th, d)) continue;
            int n = gfx_7seg_hit(px + 1, py, w, h, th, d) + gfx_7seg_hit(px - 1, py, w, h, th, d) +
                    gfx_7seg_hit(px, py + 1, w, h, th, d) + gfx_7seg_hit(px, py - 1, w, h, th, d);
            if (n) gfx_px_add(x0 + px, y0 + py, gfx_scale(glow, (uint8_t)(n > 1 ? 255 : 160)));
        }
    gfx_7seg(x0, y0, w, h, th, d, core);
}

/* ---- 26: falling blocks: the digits are built from falling pieces of up to four blocks ------------ */
static const rgb_t BLK[7] = { RGB(0, 215, 230), RGB(240, 210, 0), RGB(165, 60, 230), RGB(40, 205, 60),
                              RGB(235, 45, 40), RGB(45, 95, 245), RGB(250, 140, 20) };

static int split(uint16_t g, int8_t *pc)        /* a 3 x 5 glyph -> pieces of up to 4 touching blocks, bottom up */
{
    static const int8_t DR[4] = { 0, 0, -1, 1 }, DC[4] = { -1, 1, 0, 0 };
    int n = 0;
    for (int b = 0; b < 15; b++) pc[b] = -1;
    for (int r = 4; r >= 0; r--)
        for (int c = 0; c < 3; c++) {
            int b = r * 3 + c, got = 1;
            if (!(g & (1u << (14 - b))) || pc[b] >= 0) continue;
            pc[b] = (int8_t)n;
            for (int pass = 0; pass < 3 && got < 4; pass++)
                for (int k = 14; k >= 0 && got < 4; k--) {
                    if (pc[k] != n) continue;
                    for (int d = 0; d < 4 && got < 4; d++) {
                        int rr = k / 3 + DR[d], cc = k % 3 + DC[d], j = rr * 3 + cc;
                        if (rr < 0 || rr > 4 || cc < 0 || cc > 2 || pc[j] >= 0 || !(g & (1u << (14 - j)))) continue;
                        pc[j] = (int8_t)n; got++;
                    }
                }
            n++;
        }
    return n;
}

static void block(int x, int y, rgb_t c)
{
    gfx_px(x, y, fx_mix(c, RGB(255, 255, 255), 100)); gfx_px(x + 1, y, c);
    gfx_px(x, y + 1, c); gfx_px(x + 1, y + 1, gfx_scale(c, 150));
}

static void blocks_digit(int i, int x0, int y0, uint32_t now)
{
    int8_t pc[15];
    uint32_t el = now - C3.t[i], lead = START_WAIT;
    if (C3.old[i] >= 0) {
        lead = 420;
        if (el < lead) {                                                /* the old digit flashes and clears */
            int d = C3.old[i];
            split(gfx_tiny_glyph((char)('0' + d)), pc);
            for (int b = 0; b < 15; b++)
                if (pc[b] >= 0) block(x0 + b % 3 * 2, y0 + b / 3 * 2, ((el / 70u) & 1u) ? RGB(255, 255, 255) : BLK[(d * 3 + pc[b]) % 7]);
            return;
        }
    }
    int d = C3.cur[i], n = split(gfx_tiny_glyph((char)('0' + d)), pc);
    for (int p = 0; p < n; p++) {
        uint32_t st = lead + (uint32_t)p * 330u + (uint32_t)i * 90u;
        if (el < st) break;
        int steps = (int)((el - st) / 45u), dy = steps < 9 ? steps - 9 : 0;
        int dx = steps < 4 ? ((fx_hash((uint32_t)(d * 16 + p * 3 + i)) & 1u) ? 1 : -1) : 0;   /* slides in */
        for (int b = 0; b < 15; b++)
            if (pc[b] == p) block(x0 + (b % 3 + dx) * 2, y0 + (b / 3 + dy) * 2, BLK[(d * 3 + p) % 7]);
    }
}

static void face_blocks(const clock_tm_t *tm, int hh, int show_date, uint32_t now, unsigned ms)
{
    int v[4] = { hh / 10, hh % 10, tm->m / 10, tm->m % 10 }, y0 = show_date ? 4 : 9;
    gfx_fill(0);
    for (int i = 0; i < 4; i++) { track(i, v[i], now); blocks_digit(i, DX4[i], y0, now); }
    if (ms < 500) { block(15, y0 + 2, RGB(200, 200, 215)); block(15, y0 + 6, RGB(200, 200, 215)); }
    if (show_date) { char b[8]; date_str(tm, b); tiny_c(20, b, RGB(150, 150, 185)); }
    for (int x = 0; x < 32; x++) gfx_px(x, 31, x < (tm->s * 32 + 30) / 60 ? RGB(70, 70, 115) : RGB(16, 16, 24));
}

/* ---- 27: count the squares (TIX style): as many lit squares as the digit, in a new pattern every 4 s ---- */
static void squares_field(int x0, int y0, int cols, int value, unsigned field, uint32_t now, rgb_t c)
{
    int n = cols * 3, fade = (int)(now % 4000u) * 256 / 300;
    uint32_t per = now / 4000u;
    uint8_t lit[9], was[9];
    if (fade > 256) fade = 256;
    for (int pass = 0; pass < 2; pass++) {                             /* this pattern, and the one before */
        uint8_t ord[9], *o = pass ? was : lit;
        for (int k = 0; k < n; k++) ord[k] = (uint8_t)k;
        for (int k = n - 1; k > 0; k--) {
            int j = (int)(fx_hash((per - (uint32_t)pass) * 64u + field * 9u + (uint32_t)k) % (uint32_t)(k + 1));
            uint8_t s = ord[k]; ord[k] = ord[j]; ord[j] = s;
        }
        for (int k = 0; k < n; k++) o[ord[k]] = (uint8_t)(k < value);
    }
    for (int k = 0; k < n; k++) {
        int b = lit[k] ? (was[k] ? 256 : fade) : (was[k] ? 256 - fade : 0);
        gfx_rect(x0 + (k % cols) * 5, y0 + (k / cols) * 5, 4, 4, fx_mix(gfx_scale(c, 24), c, b), 1);
    }
}

static void face_squares(const clock_tm_t *tm, int hh, uint32_t now)
{
    gfx_fill(0);
    squares_field(8, 1, 1, hh / 10, 0, now, RGB(235, 45, 40));
    squares_field(16, 1, 3, hh % 10, 1, now, RGB(40, 205, 70));
    squares_field(3, 17, 2, tm->m / 10, 2, now, RGB(50, 115, 255));
    squares_field(16, 17, 3, tm->m % 10, 3, now, RGB(235, 45, 40));
}

/* ---- 28: abacus: a rod for every digit (HH MM SS); the bead above the beam is 5, each below is 1 ---- */
static void bead(int x, int y, rgb_t c)
{
    gfx_rect(x - 1, y, 3, 1, fx_mix(c, RGB(255, 255, 255), 80), 1);
    gfx_rect(x - 2, y + 1, 5, 1, c, 1);
    gfx_rect(x - 1, y + 2, 3, 1, gfx_scale(c, 130), 1);
}

static int slide(int a, int b, uint32_t el) { return el >= 300u ? b : a + (b - a) * ease((int)(el * 256u / 300u)) / 256; }

static void face_abacus(const clock_tm_t *tm, int hh, uint32_t now)
{
    static const int RX[6] = { 2, 7, 13, 18, 24, 29 };
    static const rgb_t COL[3] = { RGB(205, 55, 40), RGB(235, 165, 40), RGB(70, 150, 215) };
    const rgb_t wood = RGB(115, 65, 28);
    int v[6] = { hh / 10, hh % 10, tm->m / 10, tm->m % 10, tm->s / 10, tm->s % 10 };
    gfx_fill(RGB(12, 9, 7));
    for (int i = 0; i < 6; i++) gfx_rect(RX[i], 2, 1, 29, RGB(60, 42, 26), 1);
    gfx_rect(0, 0, 32, 2, wood, 1); gfx_rect(0, 8, 32, 2, wood, 1); gfx_rect(0, 31, 32, 1, wood, 1);
    for (int i = 0; i < 6; i++) {
        track(i, v[i], now);
        uint32_t el = now - C3.t[i];
        int o = C3.old[i];
        if (o < 0) { o = 0; el = el > START_WAIT ? el - START_WAIT : 0; }   /* first: from all beads at rest */
        bead(RX[i], slide(o >= 5 ? 5 : 2, v[i] >= 5 ? 5 : 2, el), COL[i / 2]);
        for (int j = 0; j < 4; j++)
            bead(RX[i], slide(j < o % 5 ? 10 + 3 * j : 19 + 3 * j, j < v[i] % 5 ? 10 + 3 * j : 19 + 3 * j, el), COL[i / 2]);
    }
}

/* ---- 29: oscilloscope: glowing green digits on the graticule, a trace drawn by a moving spot ------- */
static void face_scope(const clock_tm_t *tm, int hh, int show_date, uint32_t now, unsigned ms)
{
    int v[4] = { hh / 10, hh % 10, tm->m / 10, tm->m % 10 }, y0 = show_date ? 7 : 3;
    gfx_fill(RGB(0, 5, 2));
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            if ((x % 8 == 7 && !(y & 1)) || (y % 8 == 7 && !(x & 1))) gfx_px(x, y, RGB(0, 26, 10));
    for (int i = 0; i < 4; i++) glow_digit(DX4[i], y0, 6, 11, 1, v[i], RGB(170, 255, 185), RGB(0, 80, 25));
    if (ms < 500) {
        gfx_px(15, y0 + 3, RGB(170, 255, 185)); gfx_px(16, y0 + 3, RGB(170, 255, 185));
        gfx_px(15, y0 + 7, RGB(170, 255, 185)); gfx_px(16, y0 + 7, RGB(170, 255, 185));
    }
    if (show_date) { char b[8]; date_str(tm, b); tiny_c(0, b, RGB(90, 220, 120)); }
    int cy = show_date ? 25 : 23, amp = show_date ? 4 : 6, prev = 0;
    int beam = (int)(now % 800u) * 44 / 800 - 6, k = 5 + tm->s % 6 * 2;   /* a new frequency every second */
    for (int x = 0; x < 32; x++) {
        int y = cy + SN(x * k + (int)(now / 5u)) * amp / 128, age = beam - x;
        if (age < 0) age += 44;
        int b = 255 - age * 6;
        rgb_t c = age < 2 ? RGB(220, 255, 230) : gfx_scale(RGB(90, 255, 130), (uint8_t)(b < 30 ? 30 : b));
        int ya = x ? prev : y, lo = ya < y ? ya : y, hi = ya < y ? y : ya;
        for (int yy = lo; yy <= hi; yy++) gfx_px(x, yy, c);
        prev = y;
    }
}

/* ---- 30: clock of clocks: 24 small clocks whose hands draw the digits, all turning when one changes ---- */
enum { HU = 0, HR = 64, HD = 128, HL = 192, HN = 160 };               /* up, right, down, left, rest (7:30) */
static const uint8_t CC[10][6][2] = {                                  /* 2 x 3 clocks a digit, row by row */
    { { HR, HD }, { HL, HD }, { HU, HD }, { HU, HD }, { HU, HR }, { HU, HL } },   /* 0 */
    { { HN, HN }, { HD, HD }, { HN, HN }, { HU, HD }, { HN, HN }, { HU, HU } },   /* 1 */
    { { HR, HR }, { HL, HD }, { HR, HD }, { HU, HL }, { HU, HR }, { HL, HL } },   /* 2 */
    { { HR, HR }, { HL, HD }, { HR, HR }, { HU, HD }, { HR, HR }, { HU, HL } },   /* 3 */
    { { HD, HD }, { HD, HD }, { HU, HR }, { HU, HD }, { HN, HN }, { HU, HU } },   /* 4 */
    { { HR, HD }, { HL, HL }, { HU, HR }, { HL, HD }, { HR, HR }, { HU, HL } },   /* 5 */
    { { HR, HD }, { HL, HL }, { HU, HD }, { HL, HD }, { HU, HR }, { HU, HL } },   /* 6 */
    { { HR, HR }, { HL, HD }, { HN, HN }, { HU, HD }, { HN, HN }, { HU, HU } },   /* 7 */
    { { HR, HD }, { HL, HD }, { HU, HR }, { HU, HL }, { HU, HR }, { HU, HL } },   /* 8 */
    { { HR, HD }, { HL, HD }, { HU, HR }, { HU, HD }, { HR, HR }, { HU, HL } },   /* 9 */
};

static void face_clocks(const clock_tm_t *tm, int hh, uint32_t now)
{
    int v[4] = { hh / 10, hh % 10, tm->m / 10, tm->m % 10 }, any = 0;
    int8_t prev[4];
    for (int i = 0; i < 4; i++) { prev[i] = C3.cur[i]; any |= track(i, v[i], now); }
    if (any) {
        for (int i = 0; i < 4; i++) C3.from[i] = prev[i];
        C3.ev = now + (prev[0] < 0 ? START_WAIT : 0u);
    }
    int32_t el = (int32_t)(now - C3.ev);
    gfx_fill(0);
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 6; k++) {
            int col = k & 1, cx = 5 + (i & 1) * 12 + col * 5 + 2, cy = (i >> 1) * 17 + (k >> 1) * 5 + 2;
            int32_t e = el - ((i & 1) * 2 + col) * 120;                 /* a wave from left to right */
            int p = e <= 0 ? 0 : ease((int)(e >= 2200 ? 256 : e * 256 / 2200));
            gfx_circle(cx, cy, 2, RGB(28, 28, 36), 0);
            for (int hnd = 0; hnd < 2; hnd++) {
                int a1 = CC[v[i]][k][hnd], a0 = C3.from[i] < 0 ? HN : CC[C3.from[i]][k][hnd], a;
                if (hnd == 0) a = a0 + ((((a1 - a0) & 255) + 256) * p >> 8);        /* one turn and a bit, clockwise */
                else a = a0 - ((((a0 - a1) & 255) + 256) * p >> 8);                 /* the other hand the other way */
                int sx = SN(a), sy = CS(a);
                gfx_line(cx, cy, cx + (sx * 3 + (sx >= 0 ? 64 : -64)) / 128, cy - (sy * 3 + (sy >= 0 ? 64 : -64)) / 128, RGB(235, 235, 235));
            }
        }
}

/* ---- 31: station clock: bars for the hours, the minute hand jumps, the red second hand goes round in
 *          58.5 s and waits at the top for the minute ------------------------------------------------ */
static void face_station(const clock_tm_t *tm, uint32_t now, unsigned ms)
{
    const rgb_t ink = RGB(25, 25, 28), red = RGB(205, 30, 25);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int d = dist32[y][x];                                       /* 1/8 px from the centre */
            gfx_px(x, y, d < 118 ? RGB(236, 236, 230) : d < 127 ? RGB(70, 72, 78) : RGB(10, 10, 12));
        }
    for (int i = 0; i < 12; i++) aa_hand(248, 248, (i * 256 + 6) / 12, 186, 226, i % 3 ? 9 : 15, ink);
    track(0, tm->m, now);
    int ma = tm->m * 256 / 60 + (C3.old[0] >= 0 && now - C3.t[0] < 150u);   /* the jump overshoots a little */
    aa_hand(248, 248, ((tm->h % 12) * 256 + tm->m * 256 / 60) / 12, -40, 140, 15, ink);
    aa_hand(248, 248, ma, -48, 214, 11, ink);
    uint32_t sm = (uint32_t)tm->s * 1000u + ms;
    int sa = sm < 58500u ? (int)(sm * 256u / 58500u) : 0;
    aa_hand(248, 248, sa, -56, 150, 6, red);
    aa_hand(248, 248, sa, 150, 150, 26, red);                           /* the disc */
    aa_hand(248, 248, 0, 0, 0, 12, red);
}

/* ---- 32: cuckoo clock: the bird calls on the quarter hours (the hour's count on the hour), the
 *          pendulum swings, one weight sinks through the hour ------------------------------------------ */
static void cone(int x, int y)                                         /* a pine cone weight on its chain */
{
    gfx_px(x, y, RGB(95, 58, 26));
    gfx_rect(x - 1, y + 1, 3, 2, RGB(110, 68, 30), 1);
    gfx_px(x, y + 1, RGB(150, 100, 50));
    gfx_px(x, y + 3, RGB(80, 48, 20));
}

static void face_cuckoo(const clock_tm_t *tm, int hh, uint32_t now)
{
    if (track(0, tm->m, now)) {
        if (C3.old[0] < 0) { C3.ev = now + 1200u; C3.calls = 1; }
        else if (tm->m % 15 == 0) { C3.ev = now; C3.calls = (uint8_t)(tm->m ? 1 : (hh % 12 ? hh % 12 : 12)); }
    }
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) gfx_px(x, y, (x & 3) == 0 ? RGB(34, 22, 26) : RGB(44, 29, 33));
    int lc = tm->m * 4 / 59;                                           /* chains: 0..4 px */
    for (int y = 24; y < 24 + lc; y++) gfx_px(10, y, (y & 1) ? RGB(150, 150, 150) : RGB(90, 90, 90));
    for (int y = 24; y < 28 - lc; y++) gfx_px(21, y, (y & 1) ? RGB(150, 150, 150) : RGB(90, 90, 90));
    cone(10, 24 + lc); cone(21, 28 - lc);
    int sw = SN((int)((now % 2000u) * 256u / 2000u)) * 20 / 128;
    aa_hand(248, 352, 128 + sw, 16, 120, 6, RGB(170, 130, 50));          /* the pendulum */
    aa_hand(248, 352, 128 + sw, 132, 132, 24, RGB(225, 180, 70));
    gfx_rect(6, 7, 20, 17, RGB(140, 84, 40), 1);                        /* the case */
    gfx_rect(6, 7, 20, 17, RGB(95, 55, 25), 0);
    for (int y = 0; y <= 6; y++) {                                      /* the roof */
        int w = 1 + y * 2;
        gfx_rect(16 - w, y, 2 * w, 1, RGB(85, 45, 20), 1);
        gfx_px(16 - w, y, RGB(125, 72, 32)); gfx_px(15 + w, y, RGB(125, 72, 32));
    }
    gfx_px(15, 0, RGB(70, 150, 55)); gfx_px(16, 0, RGB(70, 150, 55)); gfx_px(3, 6, RGB(70, 150, 55)); gfx_px(28, 6, RGB(70, 150, 55));
    for (int y = 10; y <= 23; y++)                                      /* the dial: radius 5, centre (15.5, 16.5) */
        for (int x = 9; x <= 22; x++) {
            int q = (2 * x - 31) * (2 * x - 31) + (2 * y - 33) * (2 * y - 33);
            if (q < 100) gfx_px(x, y, RGB(235, 225, 195));
            else if (q < 150) gfx_px(x, y, RGB(190, 150, 70));
        }
    for (int k = 0; k < 4; k++) aa_hand(248, 264, k * 64, 66, 66, 7, RGB(120, 90, 60));
    aa_hand(248, 264, ((tm->h % 12) * 256 + tm->m * 256 / 60) / 12, -8, 50, 10, RGB(35, 25, 20));
    aa_hand(248, 264, tm->m * 256 / 60, -8, 74, 7, RGB(35, 25, 20));
    int32_t el = (int32_t)(now - C3.ev);
    int out = el >= 0 && el / 900 < C3.calls && el % 900 >= 120 && el % 900 < 760;
    if (!out) { gfx_rect(13, 8, 6, 3, RGB(105, 60, 27), 1); gfx_rect(13, 8, 6, 3, RGB(80, 45, 20), 0); return; }
    gfx_rect(13, 8, 6, 3, RGB(8, 5, 3), 1);                             /* door open, the bird out */
    gfx_rect(14, 8, 4, 3, RGB(205, 155, 60), 1);
    gfx_rect(16, 10, 3, 1, RGB(150, 100, 40), 1);
    gfx_px(15, 9, RGB(10, 10, 10));
    gfx_px(13, 9, RGB(250, 120, 20)); gfx_px(12, 9, RGB(250, 120, 20));
    if (el % 900 < 440) gfx_px(12, 10, RGB(250, 120, 20));               /* beak open: cuckoo */
}

/* ---- 33: VFD: blue-green segments glowing in the dark glass, the unlit ones faintly there ---------- */
static void face_vfd(const clock_tm_t *tm, int hh, int fmt24, int show_date, unsigned ms)
{
    static const int SX[6] = { 1, 6, 12, 17, 23, 28 };                /* DD.MM, then the seconds behind a blue filter */
    const rgb_t on = RGB(120, 255, 220), glow = RGB(0, 50, 44), ghost = RGB(9, 26, 24), blue = RGB(90, 170, 255);
    int v[4] = { hh / 10, hh % 10, tm->m / 10, tm->m % 10 };
    int sv[6] = { tm->day / 10, tm->day % 10, tm->mon / 10, tm->mon % 10, tm->s / 10, tm->s % 10 };
    gfx_fill(RGB(2, 5, 6));
    for (int x = 0; x < 32; x++) { gfx_px(x, 6, RGB(14, 18, 20)); gfx_px(x, 21, RGB(14, 18, 20)); }   /* filament wires */
    if (!fmt24 && v[0] == 0) v[0] = 10;                                 /* 12 hour: no leading zero */
    for (int i = 0; i < 4; i++) {
        gfx_7seg(DX4[i], 8, 6, 11, 2, 8, ghost);
        if (v[i] != 10) glow_digit(DX4[i], 8, 6, 11, 2, v[i], on, glow);
    }
    gfx_rect(15, 10, 2, 2, ms < 500 ? on : ghost, 1); gfx_rect(15, 15, 2, 2, ms < 500 ? on : ghost, 1);
    gfx_tiny_text(1, 0, "AM", !fmt24 && tm->h < 12 ? on : ghost);
    gfx_tiny_text(10, 0, "PM", !fmt24 && tm->h >= 12 ? on : ghost);
    if (show_date) gfx_tiny_text(31 - gfx_tiny_width(DOW[(tm->dow + 6) % 7]), 0, DOW[(tm->dow + 6) % 7], on);
    for (int i = 0; i < 6; i++) {
        gfx_7seg(SX[i], 24, 4, 7, 1, 8, ghost);
        if (i >= 4 || show_date) gfx_7seg(SX[i], 24, 4, 7, 1, sv[i], i >= 4 ? blue : on);
    }
    gfx_px(10, 30, show_date ? on : ghost);
}

/* ---- 34: flip-dot: yellow discs that flip over, column by column, when the time changes ------------ */
static void flip_dot(int x, int y, int state)                         /* 0 dark side, 1 yellow side, 2 edge on */
{
    int px = x * 2, py = y * 2;
    if (state == 1) {
        gfx_px(px, py, RGB(255, 222, 60)); gfx_px(px + 1, py, RGB(236, 196, 38));
        gfx_px(px, py + 1, RGB(226, 186, 32)); gfx_px(px + 1, py + 1, RGB(120, 92, 10));
    } else if (state == 2) {
        gfx_px(px, py, RGB(150, 140, 90)); gfx_px(px, py + 1, RGB(100, 92, 60));
    } else gfx_px(px, py, RGB(40, 40, 36));
}

static int glyph_bit(int d, int cx, int cy)                            /* is (cx, cy) of digit d lit (3 x 5)? */
{
    return d >= 0 && ((gfx_tiny_glyph((char)('0' + d)) >> (14 - cy * 3 - cx)) & 1u);
}

static void face_flipdot(const clock_tm_t *tm, int hh, uint32_t now, unsigned ms)
{
    int v[4] = { hh / 10, hh % 10, tm->m / 10, tm->m % 10 };
    int s1 = (tm->s + 1) * 16 / 60, s0 = (tm->s ? tm->s : 60) * 16 / 60;   /* lit dots of the seconds row */
    gfx_fill(RGB(6, 6, 5));
    for (int i = 0; i < 4; i++) track(i, v[i], now);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            int was = 0, is = 0;
            uint32_t at = now - ms + (uint32_t)x * 30u;
            if (y == 14) { was = x < s0; is = x < s1; }
            else
                for (int i = 0; i < 4; i++) {
                    int ox = 4 + (i & 1) * 4, oy = 1 + (i >> 1) * 7;
                    if (x < ox || x > ox + 2 || y < oy || y > oy + 4) continue;
                    was = glyph_bit(C3.old[i], x - ox, y - oy);
                    is = glyph_bit(C3.cur[i], x - ox, y - oy);
                    at = C3.t[i] + (C3.old[i] < 0 ? START_WAIT : 0u) + (uint32_t)x * 35u;
                }
            int st = is;
            if (was != is) {
                if ((int32_t)(now - at) < 0) st = was;
                else if (now - at < 90u) st = 2;
            }
            flip_dot(x, y, st);
        }
}

/* ---- 35: terminal: the time and date typed out at a prompt, every minute ---------------------------- */
static int typed(int x, int y, const char *s, int n, rgb_t c)         /* the first n characters; returns the x after */
{
    char b[12];
    int k = 0;
    for (; s[k] && k < n && k < 11; k++) b[k] = s[k];
    b[k] = 0;
    return gfx_tiny_text(x, y, b, c);
}

static void face_term(const clock_tm_t *tm, int hh, int fmt24, int show_date, uint32_t now)
{
    const rgb_t txt = RGB(90, 255, 120), dim = RGB(40, 150, 70);
    if (track(0, tm->m, now)) C3.ev = now + (C3.old[0] < 0 ? START_WAIT : 0u);
    int32_t el = (int32_t)(now - C3.ev);
    int cx = 1, cy = 1, busy = 0;
    gfx_fill(RGB(0, 7, 2));
    if (el >= 0) {
        int n = (int)(el / 110) + 1;
        cx = typed(1, 1, "$ TIME", n, dim); busy = n < 6;
        if (el >= 910) {                                                /* the answer */
            char t[6];
            hhmm(t, hh, tm->m);
            gfx_text(1, 8, t, txt);
            if (!fmt24) gfx_tiny_text(24, 1, tm->h < 12 ? "AM" : "PM", dim);
        }
        if (el >= 1200) {
            int len = show_date ? 6 : 1, k = (int)((el - 1200) / 110) + 1;
            cx = typed(1, 17, show_date ? "$ DATE" : "$", k, dim); cy = 17; busy = k < len;
            if (show_date && el >= 2110) { char d[8]; date_str(tm, d); cx = gfx_tiny_text(1, 24, d, txt) + 1; cy = 24; }
        }
    }
    if (busy || ((now / 400u) & 1u)) gfx_rect(cx, cy, 3, 5, txt, 1);
    fb_t *f = gfx_fb();                                                 /* scan lines */
    for (int y = 1; y < 32; y += 2)
        for (int x = 0; x < 32; x++)
            for (int c = 0; c < 3; c++) (*f)[y][x][c] = (uint8_t)((*f)[y][x][c] * 3 / 4);
}

/* ---- 36: odometer: number drums that roll on to the next digit ----------------------------------- */
static void drum(int x, int y, int w, int h, int pitch, int big, int vq, rgb_t bg, rgb_t ink)
{
    int base = vq >> 8, frac = vq & 255, top = y + (h - (big ? 7 : 5)) / 2;
    gfx_rect(x, y, w, h, bg, 1);
    gfx_clip(x, y, x + w, y + h);
    for (int j = -1; j <= 1; j++) {
        char ch = (char)('0' + ((base + j) % 10 + 10) % 10);
        int gy = top + j * pitch - frac * pitch / 256;
        if (big) glyph57(x + (w - 5) / 2, gy, ch, ink);
        else gfx_tiny_char(x + (w - 3) / 2, gy, ch, ink);
    }
    gfx_noclip();
    for (int r = 0; r < h; r++) {                                      /* the drum's curve: darker away from the middle */
        int e = (2 * r - (h - 1)) * 16 / (h - 1), k = 255 - e * e * 170 / 256;
        for (int c = 0; c < w; c++) gfx_px(x + c, y + r, gfx_scale(gfx_get(x + c, y + r), (uint8_t)k));
    }
}

static int roll(int i, uint32_t now)                                  /* drum i's position, 1/256 digit */
{
    int cur = C3.cur[i], old = C3.old[i];
    uint32_t wait = 0;
    if (old < 0) { old = (cur + 7) % 10; wait = START_WAIT + (uint32_t)i * 140u; }   /* first: roll in */
    int steps = (cur - old + 10) % 10, dur = 260 * steps + 120;
    int32_t el = (int32_t)(now - C3.t[i] - wait);
    if (el <= 0) return old * 256;
    if (el >= dur) return cur * 256;
    return old * 256 + steps * ease(el * 256 / dur);
}

static void face_odometer(const clock_tm_t *tm, int hh, int show_date, uint32_t now, unsigned ms)
{
    static const int MX[4] = { 1, 8, 17, 24 }, DX[4] = { 4, 10, 18, 24 };
    int v[4] = { hh / 10, hh % 10, tm->m / 10, tm->m % 10 };
    const rgb_t dark = RGB(22, 22, 24), metal = RGB(64, 64, 72);
    gfx_fill(RGB(16, 16, 20));
    gfx_rect(0, 3, 32, 17, metal, 1); gfx_rect(0, 21, 32, 11, metal, 1);
    for (int i = 0; i < 4; i++) { track(i, v[i], now); drum(MX[i], 5, 7, 13, 10, 1, roll(i, now), dark, RGB(240, 240, 230)); }
    if (ms < 500) { gfx_rect(15, 9, 2, 2, RGB(170, 170, 180), 1); gfx_rect(15, 14, 2, 2, RGB(170, 170, 180), 1); }
    if (show_date) {
        int dv[4] = { tm->day / 10, tm->day % 10, tm->mon / 10, tm->mon % 10 };
        for (int i = 0; i < 4; i++) { track(4 + i, dv[i], now); drum(DX[i], 22, 5, 9, 7, 0, roll(4 + i, now), dark, RGB(235, 205, 120)); }
        gfx_px(16, 29, RGB(170, 170, 180));
    } else
        for (int i = 0; i < 2; i++) {
            track(4 + i, i ? tm->s % 10 : tm->s / 10, now);
            drum(11 + i * 6, 22, 5, 9, 7, 0, roll(4 + i, now), RGB(150, 22, 22), RGB(250, 245, 235));
        }
}

/* ---- 37: candle: an hour candle burns down, a mark for every ten minutes; it is new on the hour ----- */
static void face_candle(const clock_tm_t *tm, int hh, int show_date, uint32_t now)
{
    int left = 3599 - (tm->m * 60 + tm->s), top = 25 - left * 18 / 3599;   /* seconds of the hour still to burn */
    uint32_t fl = fx_hash(now / 90u);
    int flick = (int)(fl & 15u), sway = SN((int)(now / 7u)) / 90;
    int fx = 22 * 16 + 8 + sway * 8, fy = (top - 4) * 16;
    for (int y = 0; y < 32; y++)                                       /* the dark, lit by the flame */
        for (int x = 0; x < 32; x++) {
            int dx = x * 16 + 8 - fx, dy = y * 16 + 8 - fy, g = 2400 / ((dx * dx + dy * dy) / 256 + 20);
            g = g * (200 + flick * 3) / 256;
            if (g > 120) g = 120;
            gfx_px(x, y, RGB(8 + g, 4 + g * 45 / 100, 2 + g / 8));
        }
    for (int y = top; y <= 28; y++) {
        gfx_px(20, y, RGB(250, 242, 220));
        gfx_rect(21, y, 4, 1, RGB(232, 220, 192), 1);
        gfx_px(25, y, RGB(188, 172, 142));
    }
    gfx_rect(20, top, 6, 1, RGB(255, 248, 228), 1);
    for (int k = 1; k <= 5; k++) {                                      /* the marks still to burn */
        int y = 25 - (3599 - k * 600) * 18 / 3599;
        if (y > top + 1) gfx_rect(20, y, 2, 1, RGB(200, 45, 35), 1);
    }
    gfx_px(22, top - 1, RGB(50, 35, 25));                               /* the wick and the flame */
    int fh = 5 + (int)((fl >> 4) & 1u);
    for (int r = 0; r < fh; r++) {
        int y = top - 2 - r, w = r == 0 ? 1 : r < 4 ? 3 : 1, x0 = 22 - w / 2 + (r >= 3 ? sway : 0);
        for (int k = 0; k < w; k++) {
            rgb_t c = r == 0 ? RGB(90, 110, 255) : r == fh - 1 ? RGB(230, 100, 20)
                    : (w == 3 && k != 1) ? RGB(245, 120, 25) : r < 3 ? RGB(255, 248, 210) : RGB(255, 200, 70);
            gfx_px(x0 + k, y, c);
        }
    }
    gfx_rect(16, 29, 14, 1, RGB(205, 155, 60), 1); gfx_rect(15, 30, 16, 1, RGB(150, 105, 40), 1);
    rgb_t ink = gfx_scale(RGB(255, 196, 120), (uint8_t)(215 + flick * 2));
    char b[3];
    two(b, (unsigned)hh); gfx_text(2, 4, b, ink);
    two(b, tm->m); gfx_text(2, 14, b, ink);
    if (show_date) gfx_tiny_text(2, 25, DOW[(tm->dow + 6) % 7], gfx_scale(ink, 170));
}

/* ---- 38: decimal time (France, 1794): 10 hours of 100 minutes of 100 seconds a day ---------------- */
static void face_decimal(const clock_tm_t *tm, int hh, unsigned ms)
{
    uint32_t dms = ((uint32_t)tm->h * 3600u + tm->m * 60u + tm->s) * 1000u + ms;
    uint32_t dec = dms / 864u;                                           /* decimal seconds into the day, 0..99999 */
    gfx_fill(0);
    for (int y = 0; y < 22; y++)                                       /* the dial: centre (15.5, 10.5) */
        for (int x = 0; x < 32; x++) {
            int q = (2 * x - 31) * (2 * x - 31) + (2 * y - 21) * (2 * y - 21);
            if (q < 19 * 19) gfx_px(x, y, RGB(22, 26, 52));
            else if (q < 21 * 21) gfx_px(x, y, RGB(200, 160, 60));
        }
    for (int k = 0; k < 10; k++) aa_hand(248, 168, (k * 256 + 5) / 10, 120, 120, k ? 6 : 9, RGB(230, 190, 90));
    aa_hand(248, 168, (int)(dec * 256u / 100000u), -16, 76, 12, RGB(240, 190, 80));
    aa_hand(248, 168, (int)(dec % 10000u * 256u / 10000u), -24, 112, 8, RGB(235, 235, 245));
    aa_hand(248, 168, (int)(dec % 100u * 256u / 100u), 0, 116, 4, RGB(220, 50, 40));
    char b[8] = { (char)('0' + dec / 10000u), ':', (char)('0' + dec / 1000u % 10u), (char)('0' + dec / 100u % 10u), ':',
                  (char)('0' + dec / 10u % 10u), (char)('0' + dec % 10u), 0 };
    tiny_c(22, b, RGB(255, 200, 90));
    char t[6];
    hhmm(t, hh, tm->m);
    tiny_c(27, t, RGB(110, 120, 160));
}

/* ---- 39: radar: the sweep lights up the hour (near) and the minute (far) ------------------------- */
static void blip(int a, int r, int hw, int sw, rgb_t c)
{
    int since = (sw - a) & 255;                                        /* how long ago the sweep passed it */
    aa_hand(248, 248, a, r, r, hw, gfx_scale(c, (uint8_t)(255 - since * 190 / 256)));
}

static void face_radar(const clock_tm_t *tm, int hh, int show_date, uint32_t now)
{
    int sw = (int)((now % 4000u) * 256u / 4000u);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int d = dist32[y][x], a = (ang32[y][x] + 64) & 255, behind = (sw - a) & 255;
            if (d > 124) { gfx_px(x, y, 0); continue; }
            rgb_t c = RGB(0, 16, 6);
            if ((d > 36 && d < 44) || (d > 76 && d < 84) || d > 116) c = RGB(0, 55, 22);
            else if ((x == 15 || x == 16 || y == 15 || y == 16) && ((x + y) & 1)) c = RGB(0, 40, 16);
            if (behind < 72) c = fx_mix(c, RGB(60, 255, 120), (72 - behind) * 200 / 72);
            gfx_px(x, y, c);
        }
    blip(((tm->h % 12) * 256 + tm->m * 256 / 60) / 12, 96, 26, sw, RGB(255, 235, 140));
    blip(tm->m * 256 / 60 + tm->s * 256 / 3600, 176, 18, sw, RGB(200, 255, 210));
    char b[3];
    const rgb_t ink = RGB(120, 255, 150);
    two(b, (unsigned)hh); gfx_tiny_text(0, 0, b, ink);
    two(b, tm->m); gfx_tiny_text(32 - gfx_tiny_width(b), 0, b, ink);
    two(b, tm->s); gfx_tiny_text(32 - gfx_tiny_width(b), 27, b, gfx_scale(ink, 150));
    if (show_date) { two(b, tm->day); gfx_tiny_text(0, 27, b, gfx_scale(ink, 150)); }
}

/* ---- 40: ticker: the time above, the date and seconds running past below ------------------------- */
static void face_ticker(const clock_tm_t *tm, int hh, int show_date, uint32_t now)
{
    char t[6], line[40];
    int n = 0;
    hhmm(t, hh, tm->m);
    if (show_date) {
        const char *d = DOW[(tm->dow + 6) % 7], *mo = MON[(tm->mon + 11) % 12];
        for (int k = 0; k < 3; k++) line[n++] = d[k];
        line[n++] = ' '; two(line + n, tm->day); n += 2; line[n++] = ' ';
        for (int k = 0; k < 3; k++) line[n++] = mo[k];
        line[n++] = ' '; two(line + n, tm->year / 100u); two(line + n + 2, tm->year % 100u); n += 4;
        line[n++] = ' '; line[n++] = ' ';
    }
    for (int k = 0; k < 5; k++) line[n++] = t[k];
    line[n++] = ':'; two(line + n, tm->s); n += 2;
    line[n++] = ' '; line[n++] = ' '; line[n++] = ' '; line[n] = 0;
    gfx_fill(RGB(8, 3, 0));
    gfx_text((32 - gfx_text_width(t)) / 2, 3, t, RGB(255, 170, 40));
    gfx_rect(0, 13, 32, 1, RGB(70, 22, 0), 1); gfx_rect(0, 25, 32, 1, RGB(70, 22, 0), 1);
    gfx_rect(0, 14, 32, 11, RGB(30, 8, 0), 1);
    int w = gfx_text_width(line), off = (int)(now / 40u % (uint32_t)w);
    gfx_clip(0, 14, 32, 25);
    gfx_text(-off, 16, line, RGB(255, 70, 30));
    gfx_text(w - off, 16, line, RGB(255, 70, 30));
    gfx_noclip();
    gfx_rect(1, 28, (tm->s * 30 + 30) / 60, 2, RGB(120, 50, 10), 1);
}

/* ---- 41: melting clock: a watch draped over a branch, its lower half running down ----------------- */
static int seg_d2(int px, int py, int vx, int vy)                    /* squared distance from p to the segment 0..v */
{
    int t = px * vx + py * vy, l2 = vx * vx + vy * vy;
    if (t > 0 && l2) {
        if (t >= l2) { px -= vx; py -= vy; }
        else { px -= vx * t / l2; py -= vy * t / l2; }
    }
    return px * px + py * py;
}

static void face_melt(const clock_tm_t *tm, uint32_t now)
{
    const int cx = 248, cy = 160, R = 150;                              /* the watch, unmelted: 1/16 px */
    int ha = ((tm->h % 12) * 256 + tm->m * 256 / 60) / 12, ma = tm->m * 256 / 60, sa = tm->s * 256 / 60;
    int hx = SN(ha) * 80 / 127, hy = -CS(ha) * 80 / 127, mx = SN(ma) * 122 / 127, my = -CS(ma) * 122 / 127;
    int sx = SN(sa) * 126 / 127, sy = -CS(sa) * 126 / 127, mk[12][2];
    for (int k = 0; k < 12; k++) { mk[k][0] = SN(k * 256 / 12) * 114 / 127; mk[k][1] = -CS(k * 256 / 12) * 114 / 127; }
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int X = x * 16 + 8, Y = y * 16 + 8, U = X, V = Y;
            if (Y > 176) {                                              /* below the fold it hangs and stretches */
                int b = 240 - (x > 20 ? x - 20 : 20 - x) * 16;
                if (b < 0) b = 0;
                int s = 256 + b + SN((int)(now / 30u) + x * 9) * 14 / 128;
                V = 176 + (Y - 176) * 256 / s;
                U = X + (Y - 176) * b / 1000;
            }
            rgb_t c = y < 22 ? fx_mix(RGB(70, 110, 170), RGB(245, 200, 120), y * 256 / 22) : fx_mix(RGB(125, 88, 46), RGB(70, 48, 26), (y - 22) * 256 / 10);
            int dx = U - cx, dy = V - cy, d2 = dx * dx + dy * dy;
            if (d2 < (R + 12) * (R + 12)) {
                if (d2 >= (R - 14) * (R - 14)) c = RGB(215, 175, 75);          /* the gold case */
                else {
                    c = RGB(242, 234, 205);
                    for (int k = 0; k < 12; k++)
                        if ((dx - mk[k][0]) * (dx - mk[k][0]) + (dy - mk[k][1]) * (dy - mk[k][1]) < 12 * 12) c = RGB(90, 80, 70);
                    if (seg_d2(dx, dy, hx, hy) < 20 * 20 || seg_d2(dx, dy, mx, my) < 14 * 14) c = RGB(30, 25, 25);
                    if (seg_d2(dx, dy, sx, sy) < 7 * 7) c = RGB(200, 40, 30);
                }
            }
            gfx_px(x, y, c);
        }
    gfx_line(0, 12, 6, 11, RGB(70, 45, 25)); gfx_line(0, 13, 6, 12, RGB(55, 35, 20));   /* the branch */
    uint32_t dp = now % 2500u;                                         /* now and then a drop falls */
    if (dp < 700u) gfx_px(20, 28 + (int)(dp * dp / 120000u), RGB(235, 215, 160));
}

/* ---- 42: 3D digits: thick digits with depth, a light sweeping over them ---------------------------- */
static void face_3d(const clock_tm_t *tm, int hh, uint32_t now)
{
    static const int X[4] = { 3, 17, 3, 17 }, Y[4] = { 1, 1, 17, 17 };
    static const rgb_t TOP[2] = { RGB(255, 150, 60), RGB(90, 210, 255) }, LOW[2] = { RGB(230, 40, 90), RGB(70, 80, 235) };
    int v[4] = { hh / 10, hh % 10, tm->m / 10, tm->m % 10 };
    int band = (int)(now % 2600u) * 70 / 2600 - 20;
    gfx_fill(RGB(5, 5, 12));
    for (int i = 0; i < 4; i++) {
        int r = i >> 1;
        gfx_7seg(X[i] + 2, Y[i] + 2, 10, 13, 3, v[i], gfx_scale(LOW[r], 70));
        gfx_7seg(X[i] + 1, Y[i] + 1, 10, 13, 3, v[i], gfx_scale(LOW[r], 120));
        for (int py = 0; py < 13; py++)
            for (int px = 0; px < 10; px++) {
                if (!gfx_7seg_hit(px, py, 10, 13, 3, v[i])) continue;
                rgb_t c = fx_mix(TOP[r], LOW[r], py * 256 / 12);
                int dd = X[i] + px + Y[i] + py - band;
                if (dd > -3 && dd < 3) c = fx_mix(c, RGB(255, 255, 255), (3 - (dd < 0 ? -dd : dd)) * 70);
                gfx_px(X[i] + px, Y[i] + py, c);
            }
    }
}

/* ---- dispatch ----------------------------------------------------------------------------------- */
static uint8_t  last_style = 255;
static uint32_t last_now;

void clock3_draw(unsigned style, const clock_tm_t *tm, int hh, int fmt24, int show_date, uint32_t now, unsigned ms)
{
    style %= CLOCK3_STYLES;
    if (style != last_style || now - last_now > 250u) {               /* (re)started: fresh state */
        for (int i = 0; i < 8; i++) { C3.cur[i] = -1; C3.old[i] = -1; C3.t[i] = now; }
        for (int i = 0; i < 4; i++) C3.from[i] = -1;
        C3.ev = now; C3.calls = 0;
    }
    last_style = (uint8_t)style; last_now = now;
    switch (style) {
    case 0:  face_blocks(tm, hh, show_date, now, ms); break;
    case 1:  face_squares(tm, hh, now); break;
    case 2:  face_abacus(tm, hh, now); break;
    case 3:  face_scope(tm, hh, show_date, now, ms); break;
    case 4:  face_clocks(tm, hh, now); break;
    case 5:  face_station(tm, now, ms); break;
    case 6:  face_cuckoo(tm, hh, now); break;
    case 7:  face_vfd(tm, hh, fmt24, show_date, ms); break;
    case 8:  face_flipdot(tm, hh, now, ms); break;
    case 9:  face_term(tm, hh, fmt24, show_date, now); break;
    case 10: face_odometer(tm, hh, show_date, now, ms); break;
    case 11: face_candle(tm, hh, show_date, now); break;
    case 12: face_decimal(tm, hh, ms); break;
    case 13: face_radar(tm, hh, show_date, now); break;
    case 14: face_ticker(tm, hh, show_date, now); break;
    case 15: face_melt(tm, now); break;
    default: face_3d(tm, hh, now); break;
    }
}
