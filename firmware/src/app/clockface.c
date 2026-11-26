/* Time keeping (synced over BLE, runs on the 1 ms tick) and clock faces. */
#include "clockface.h"
#include "clockface2.h"
#include "gfx.h"
#include "tables.h"
#include "effects.h"

_Static_assert(CLOCK_STYLES == 10 + CLOCK2_STYLES, "clockface.h and clockface2.h disagree on the number of faces");

static uint32_t base_sec = 12u * 3600u, base_ms;   /* unsynced clocks start at 12:00:00 */
static uint8_t  synced;
static uint16_t d_year = 2026;
static uint8_t  d_mon = 1, d_day = 1, d_dow = 4;

static const char *const DOW[7]  = { "MON","TUE","WED","THU","FRI","SAT","SUN" };

static unsigned mdays(unsigned y, unsigned m)
{
    static const uint8_t D[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    return (m == 2 && (y % 4u) == 0) ? 29u : D[m - 1];
}

void clock_set_time(uint8_t h, uint8_t m, uint8_t s, uint32_t now_ms)
{
    if (h > 23 || m > 59 || s > 59) return;
    base_sec = h * 3600u + m * 60u + s;
    base_ms = now_ms;
    synced = 1;
}

void clock_set_date(uint8_t yy, uint8_t mon, uint8_t day, uint8_t dow)
{
    if (mon < 1 || mon > 12 || day < 1 || day > 31) return;
    d_year = (uint16_t)(2000u + yy); d_mon = mon; d_day = day;
    if (dow >= 1 && dow <= 7) d_dow = dow;
}

void clock_now(uint32_t now_ms, clock_tm_t *tm)
{
    uint32_t total = base_sec + (now_ms - base_ms) / 1000u;
    uint32_t days = total / 86400u, sod = total % 86400u;
    tm->h = (uint8_t)(sod / 3600u); tm->m = (uint8_t)((sod / 60u) % 60u); tm->s = (uint8_t)(sod % 60u);
    unsigned y = d_year, mo = d_mon, d = d_day, w = d_dow;
    while (days--) {
        if (++d > mdays(y, mo)) { d = 1; if (++mo > 12) { mo = 1; y++; } }
        if (++w > 7) w = 1;
    }
    tm->year = (uint16_t)y; tm->mon = (uint8_t)mo; tm->day = (uint8_t)d; tm->dow = (uint8_t)w;
    tm->synced = synced;
}

static void hand(int len, uint8_t ang, rgb_t c)
{
    int x = 15 + ((int)sin8[ang] - 128) * len / 127;
    int y = 15 - ((int)cos8(ang) - 128) * len / 127;
    gfx_line(15, 15, x, y, c);
}

static void digits_hhmm(int y, int w, int h, int th, int hh, int mm, uint32_t t, int colon, int white)
{
    static const int X[4] = { 1, 8, 18, 25 };
    int d[4] = { hh / 10, hh % 10, mm / 10, mm % 10 };
    for (int i = 0; i < 4; i++)
        gfx_7seg(X[i], y, w, h, th, d[i], white ? RGB(255, 255, 255) : gfx_hsv((uint8_t)(t / 40 + i * 20), 230, 255));
    if (colon) { gfx_rect(15, y + 2, 2, 2, RGB(230, 230, 230), 1); gfx_rect(15, y + h - 4, 2, 2, RGB(230, 230, 230), 1); }
}

static void two_digits(char *b, unsigned v) { b[0] = (char)('0' + v / 10u % 10u); b[1] = (char)('0' + v % 10u); b[2] = 0; }

static int lerp(int a, int b, int num, int den) { return a + (b - a) * num / den; }

static rgb_t lerp_rgb(rgb_t a, rgb_t b, int num, int den)
{
    return RGB(lerp((int)(a >> 16) & 255, (int)(b >> 16) & 255, num, den),
               lerp((int)(a >> 8) & 255, (int)(b >> 8) & 255, num, den),
               lerp((int)a & 255, (int)b & 255, num, den));
}

/* ---- 5: flip cards ------------------------------------------------------------------------- */
#define FLIP_MS  360
#define CARD_Y   5                     /* card rows 5..25, hinge on row 15 */
static struct { int8_t shown[2], prev[2]; uint32_t t0[2]; } flip = { { -1, -1 }, { -1, -1 }, { 0, 0 } };

static void card_part(int cx, int v, int ya, int yb, uint8_t shade)   /* card + digits, rows [ya, yb) only */
{
    if (yb <= ya) return;
    gfx_clip(cx, ya, cx + 15, yb);
    gfx_rect(cx, CARD_Y, 15, 21, gfx_scale(RGB(34, 34, 40), shade), 1);
    rgb_t ink = gfx_scale(RGB(240, 236, 220), shade);
    gfx_7seg(cx + 1, CARD_Y + 3, 5, 15, 2, v / 10, ink);
    gfx_7seg(cx + 9, CARD_Y + 3, 5, 15, 2, v % 10, ink);
    gfx_noclip();
}

static void flip_card(int i, int cx, int v, uint32_t now)
{
    if (flip.shown[i] < 0) flip.shown[i] = flip.prev[i] = (int8_t)v;
    if (v != flip.shown[i]) { flip.prev[i] = flip.shown[i]; flip.shown[i] = (int8_t)v; flip.t0[i] = now; }
    uint32_t p = now - flip.t0[i];
    int nv = flip.shown[i], ov = flip.prev[i];
    if (p >= FLIP_MS || ov == nv) {
        card_part(cx, nv, CARD_Y, CARD_Y + 10, 255);
        card_part(cx, nv, CARD_Y + 11, CARD_Y + 21, 255);
    } else if (p < FLIP_MS / 2) {                    /* old top half folds down onto the hinge */
        int k = (int)p * 10 / (FLIP_MS / 2);
        card_part(cx, nv, CARD_Y, CARD_Y + 10, 170);
        card_part(cx, ov, CARD_Y + k, CARD_Y + 10, (uint8_t)(255 - k * 10));
        card_part(cx, ov, CARD_Y + 11, CARD_Y + 21, 255);
    } else {                                         /* new bottom half unfolds from the hinge */
        int k = ((int)p - FLIP_MS / 2) * 10 / (FLIP_MS / 2);
        card_part(cx, nv, CARD_Y, CARD_Y + 10, 255);
        card_part(cx, ov, CARD_Y + 11, CARD_Y + 21, 170);
        card_part(cx, nv, CARD_Y + 11, CARD_Y + 12 + k, (uint8_t)(150 + k * 10));
    }
    gfx_px(cx, CARD_Y, 0); gfx_px(cx + 14, CARD_Y, 0); gfx_px(cx, CARD_Y + 20, 0); gfx_px(cx + 14, CARD_Y + 20, 0);
    gfx_rect(cx, CARD_Y + 10, 15, 1, RGB(4, 4, 6), 1);                       /* hinge */
}

static void draw_flip(int hh, const clock_tm_t *tm, uint32_t now, int colon, int show_date)
{
    gfx_fill(0);
    flip_card(0, 0, hh, now);
    flip_card(1, 17, tm->m, now);
    rgb_t dot = colon ? RGB(200, 200, 190) : RGB(60, 60, 60);
    gfx_px(15, 11, dot); gfx_px(16, 11, dot); gfx_px(15, 19, dot); gfx_px(16, 19, dot);
    if (show_date) {
        char b[8] = { 0 };
        const char *dw = DOW[tm->dow - 1];
        b[0] = dw[0]; b[1] = dw[1]; b[2] = dw[2]; b[3] = ' ';
        two_digits(b + 4, tm->day);
        gfx_tiny_text((32 - gfx_tiny_width(b)) / 2, 27, b, RGB(150, 150, 160));
    } else {
        gfx_rect(1, 28, (tm->s * 30 + 30) / 60, 1, RGB(90, 90, 100), 1);
    }
}

/* ---- 6: nixie tubes ------------------------------------------------------------------------ */
static void nixie_tube(int x, int digit, uint32_t now, int i)
{
    gfx_rect(x, 3, 7, 26, RGB(18, 26, 34), 0);                                /* glass */
    gfx_px(x, 3, 0); gfx_px(x + 6, 3, 0);
    gfx_rect(x + 2, 2, 3, 1, RGB(40, 44, 50), 1);                             /* tip */
    for (int yy = 6; yy < 26; yy += 3) gfx_rect(x + 1, yy, 5, 1, RGB(6, 7, 8), 1);   /* mesh */
    gfx_7seg(x + 1, 7, 5, 17, 1, 8, RGB(26, 9, 2));                           /* unlit cathodes */
    gfx_7seg(x, 6, 7, 19, 3, digit, RGB(55, 18, 2));                          /* glow */
    uint8_t flick = (uint8_t)(225 + ((sin8[(uint8_t)(now / 3 + i * 71)] * sin8[(uint8_t)(now / 7 + i * 29)]) >> 11));
    gfx_7seg(x + 1, 7, 5, 17, 1, digit, gfx_scale(RGB(255, 125, 30), flick));
    gfx_rect(x, 29, 7, 2, RGB(30, 30, 34), 1);                                /* socket */
}

static void draw_nixie(int hh, const clock_tm_t *tm, uint32_t now, int colon)
{
    gfx_fill(0);
    nixie_tube(0, hh / 10, now, 0);
    nixie_tube(8, hh % 10, now, 1);
    nixie_tube(17, tm->m / 10, now, 2);
    nixie_tube(25, tm->m % 10, now, 3);
    rgb_t c = colon ? RGB(255, 110, 25) : RGB(40, 14, 3);
    gfx_px(15, 12, c); gfx_px(16, 12, c); gfx_px(15, 20, c); gfx_px(16, 20, c);
}

/* ---- 7: rings (hours outside, minutes, seconds inside) ------------------------------------- */
static void draw_rings(int hh, const clock_tm_t *tm, uint32_t now)
{
    uint32_t ms = (now - base_ms) % 1000u;
    unsigned fh = ((tm->h % 12u) * 60u + tm->m) * 256u / 720u;
    unsigned fm = (tm->m * 60u + tm->s) * 256u / 3600u;
    unsigned fs = (tm->s * 1000u + ms) * 256u / 60000u;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            unsigned d = dist32[y][x], a = (uint8_t)(ang32[y][x] + 64);        /* 0 = 12 o'clock, clockwise */
            rgb_t c = 0;
            if (d >= 100 && d < 124)     c = a < fh ? gfx_hsv((uint8_t)(0 + a / 16), 230, (uint8_t)(120 + a * 135 / (fh + 1))) : RGB(28, 5, 6);
            else if (d >= 76 && d < 96)  c = a < fm ? gfx_hsv((uint8_t)(80 + a / 16), 230, (uint8_t)(120 + a * 135 / (fm + 1))) : RGB(4, 22, 8);
            else if (d >= 52 && d < 72)  c = a < fs ? gfx_hsv((uint8_t)(150 + a / 16), 220, (uint8_t)(110 + a * 145 / (fs + 1))) : RGB(4, 8, 26);
            gfx_px(x, y, c);
        }
    char b[3];
    two_digits(b, (unsigned)hh); gfx_tiny_text(13, 10, b, RGB(255, 200, 180));
    two_digits(b, tm->m);        gfx_tiny_text(13, 17, b, RGB(190, 255, 200));
}

/* ---- 8: day/night sky - sun and moon move with the time ----------------------------------- */
static const struct { uint16_t min; rgb_t top, low; } SKY[] = {
    {    0, RGB(2, 3, 14),    RGB(6, 8, 28)    },
    {  300, RGB(3, 4, 18),    RGB(14, 10, 40)  },
    {  360, RGB(40, 40, 110), RGB(235, 105, 40) },
    {  450, RGB(60, 120, 220), RGB(165, 195, 235) },
    {  720, RGB(40, 110, 240), RGB(140, 200, 255) },
    { 1020, RGB(50, 110, 220), RGB(175, 180, 220) },
    { 1110, RGB(60, 40, 120), RGB(245, 95, 40)  },
    { 1200, RGB(10, 10, 45),  RGB(40, 20, 60)  },
    { 1320, RGB(2, 3, 14),    RGB(6, 8, 28)    },
    { 1440, RGB(2, 3, 14),    RGB(6, 8, 28)    },
};

static void draw_sky(int hh, const clock_tm_t *tm, uint32_t now)
{
    int mod = tm->h * 60 + tm->m;
    unsigned k = 0;
    while (k + 2 < sizeof SKY / sizeof SKY[0] && SKY[k + 1].min <= mod) k++;
    int span = SKY[k + 1].min - SKY[k].min, pos = mod - SKY[k].min;
    rgb_t top = lerp_rgb(SKY[k].top, SKY[k + 1].top, pos, span);
    rgb_t low = lerp_rgb(SKY[k].low, SKY[k + 1].low, pos, span);
    for (int y = 0; y < 32; y++) gfx_rect(0, y, 32, 1, lerp_rgb(top, low, y > 25 ? 25 : y, 25), 1);

    unsigned light = ((top >> 16) & 255) + ((top >> 8) & 255) + (top & 255);
    if (light < 90) {                                                          /* stars */
        for (unsigned i = 0; i < 22; i++) {
            unsigned hsh = i * 2654435761u;
            int sx = (int)(hsh >> 27), sy = (int)((hsh >> 22) & 31) % 20;
            uint8_t tw = sin8[(uint8_t)(now / 9 + i * 37)];
            gfx_px_add(sx, sy, gfx_scale(RGB(200, 200, 230), (uint8_t)((90 - light) * tw / 90)));
        }
    }
    if (mod >= 360 && mod < 1110) {                                            /* sun, 6:00 - 18:30 */
        int f = (mod - 360) * 256 / 750;
        int sx = 3 + f * 26 / 256, sy = 25 - ((int)sin8[(uint8_t)(f / 2)] - 128) * 15 / 127;
        for (int dy = -4; dy <= 4; dy++)                                       /* halo */
            for (int dx = -4; dx <= 4; dx++) {
                int d2 = dx * dx + dy * dy;
                if (d2 > 4 && d2 <= 18) gfx_px_add(sx + dx, sy + dy, RGB(70 - d2 * 3, 55 - d2 * 3, 20 - d2));
            }
        gfx_circle(sx, sy, 2, RGB(255, 225, 100), 1);
    } else {                                                                   /* moon, 18:30 - 6:00 */
        int f = ((mod - 1110 + 1440) % 1440) * 256 / 690;
        if (f > 255) f = 255;
        int sx = 3 + f * 26 / 256, sy = 25 - ((int)sin8[(uint8_t)(f / 2)] - 128) * 15 / 127;
        gfx_circle(sx, sy, 2, RGB(215, 215, 235), 1);
        gfx_circle(sx + 1, sy - 1, 1, lerp_rgb(top, low, sy < 0 ? 0 : sy, 25), 1);   /* crescent */
    }
    unsigned day = light > 300 ? 255 : 40 + light * 215 / 300;
    for (int x = 0; x < 32; x++) {                                             /* hills */
        int hy = 26 + ((int)sin8[(uint8_t)(x * 9)] - 128) * 2 / 127 + ((int)sin8[(uint8_t)(x * 23 + 40)] - 128) / 127;
        for (int y = hy; y < 32; y++) gfx_px(x, y, gfx_scale(RGB(20, 90, 30), (uint8_t)(day * (32 - y + 4) / 12 > 255 ? 255 : day * (32 - y + 4) / 12)));
    }
    char b[6];
    two_digits(b, (unsigned)hh); b[2] = ':'; two_digits(b + 3, tm->m);
    int tx = (32 - gfx_tiny_width(b)) / 2;
    gfx_tiny_text(tx + 1, 2, b, RGB(0, 0, 0));
    gfx_tiny_text(tx, 1, b, RGB(255, 255, 255));
}

/* ---- 9: pong clock - the hour player misses when the minute changes ------------------------ */
static struct { int16_t bx, by, vx, vy, lp, rp; int8_t miss; uint8_t last_h, last_m, init; } pc;

static void pong_serve(int dir)
{
    pc.bx = 16 << 4; pc.by = (int16_t)((10 + gfx_rnd(18)) << 4);
    pc.vx = (int16_t)(dir * (9 + (int)gfx_rnd(4)));
    pc.vy = (int16_t)((int)gfx_rnd(13) - 6);
    if (!pc.vy) pc.vy = 3;
    pc.miss = 0;
}

static void paddle_follow(int16_t *p, int target, int away)
{
    int want = away ? (target < (19 << 4) ? 29 << 4 : 9 << 4) : target;
    if (*p < want - 6) *p = (int16_t)(*p + 7); else if (*p > want + 6) *p = (int16_t)(*p - 7);
    if (*p < (9 << 4)) *p = 9 << 4;
    if (*p > (29 << 4)) *p = 29 << 4;
}

static void draw_pong(int hh, const clock_tm_t *tm)
{
    if (!pc.init) { pc.init = 1; pc.lp = pc.rp = 19 << 4; pc.last_h = tm->h; pc.last_m = tm->m; pong_serve(1); }
    if (tm->m != pc.last_m) { pc.last_m = tm->m; pc.miss = (tm->h != pc.last_h) ? 2 : 1; pc.last_h = tm->h; }
    pc.bx = (int16_t)(pc.bx + pc.vx); pc.by = (int16_t)(pc.by + pc.vy);
    if (pc.by < (7 << 4)) { pc.by = 7 << 4; pc.vy = (int16_t)-pc.vy; }
    if (pc.by > (31 << 4)) { pc.by = 31 << 4; pc.vy = (int16_t)-pc.vy; }
    paddle_follow(&pc.lp, pc.vx < 0 ? pc.by : 19 << 4, pc.miss == 1 && pc.vx < 0);
    paddle_follow(&pc.rp, pc.vx > 0 ? pc.by : 19 << 4, pc.miss == 2 && pc.vx > 0);
    int bx = pc.bx >> 4, by = pc.by >> 4;
    if (bx <= 2 && pc.vx < 0 && !(pc.miss == 1) && by >= (pc.lp >> 4) - 3 && by <= (pc.lp >> 4) + 3) {
        pc.vx = (int16_t)-pc.vx; pc.vy = (int16_t)(pc.vy + (by - (pc.lp >> 4)) * 2);
    }
    if (bx >= 29 && pc.vx > 0 && !(pc.miss == 2) && by >= (pc.rp >> 4) - 3 && by <= (pc.rp >> 4) + 3) {
        pc.vx = (int16_t)-pc.vx; pc.vy = (int16_t)(pc.vy + (by - (pc.rp >> 4)) * 2);
    }
    if (pc.vy > 12) pc.vy = 12;
    if (pc.vy < -12) pc.vy = -12;
    if (bx < -2) pong_serve(1);
    if (bx > 33) pong_serve(-1);

    gfx_fill(0);
    char b[3];
    two_digits(b, (unsigned)hh); gfx_tiny_text(6, 0, b, RGB(255, 255, 255));
    two_digits(b, tm->m);        gfx_tiny_text(19, 0, b, RGB(255, 255, 255));
    gfx_rect(0, 6, 32, 1, RGB(50, 50, 50), 1);
    for (int y = 8; y < 32; y += 3) gfx_rect(15, y, 2, 1, RGB(45, 45, 45), 1);
    gfx_rect(1, (pc.lp >> 4) - 2, 1, 5, RGB(255, 255, 255), 1);
    gfx_rect(30, (pc.rp >> 4) - 2, 1, 5, RGB(255, 255, 255), 1);
    gfx_px(bx, by, RGB(255, 255, 255));
}

void clock_draw(unsigned style, uint32_t now, int fmt24, int show_date)
{
    clock_tm_t tm;
    clock_now(now, &tm);
    int hh = tm.h;
    if (!fmt24) { hh %= 12; if (!hh) hh = 12; }
    unsigned ms = (now - base_ms) % 1000u;
    int colon = ms < 500u;

    switch (style % CLOCK_STYLES) {
    case 0: {                                                     /* digital + date */
        gfx_fill(0);
        digits_hhmm(2, 6, 11, 2, hh, tm.m, now, colon, 0);
        gfx_rect(0, 15, (tm.s * 32 + 30) / 60, 1, tm.synced ? RGB(0, 90, 160) : RGB(150, 60, 0), 1);
        if (show_date) {
            char buf[6] = { (char)('0' + tm.day / 10), (char)('0' + tm.day % 10), '.',
                            (char)('0' + tm.mon / 10), (char)('0' + tm.mon % 10), 0 };
            gfx_text((32 - gfx_text_width(buf)) / 2, 17, buf, RGB(200, 200, 200));
            const char *dw = DOW[tm.dow - 1];
            gfx_text((32 - gfx_text_width(dw)) / 2, 25, dw, RGB(0, 170, 200));
        }
        break; }
    case 1: {                                                     /* analog */
        gfx_fill(0);
        for (int i = 0; i < 12; i++) {
            uint8_t a = (uint8_t)(i * 256 / 12);
            int x = 15 + ((int)sin8[a] - 128) * 14 / 127, y = 15 - ((int)cos8(a) - 128) * 14 / 127;
            gfx_px(x, y, (i % 3) ? RGB(70, 70, 90) : RGB(220, 220, 255));
        }
        hand(8,  (uint8_t)((tm.h % 12) * 256 / 12 + tm.m * 256 / 720), RGB(255, 170, 60));
        hand(12, (uint8_t)(tm.m * 256 / 60 + tm.s * 256 / 3600), RGB(120, 220, 255));
        hand(14, (uint8_t)(tm.s * 256 / 60), RGB(255, 40, 40));
        gfx_px(15, 15, RGB(255, 255, 255));
        break; }
    case 2: {                                                     /* binary (BCD) */
        gfx_fill(0);
        int v[6] = { hh / 10, hh % 10, tm.m / 10, tm.m % 10, tm.s / 10, tm.s % 10 };
        static const uint8_t HUE[3] = { 0, 90, 165 };
        for (int c = 0; c < 6; c++)
            for (int b = 0; b < 4; b++) {
                int on = (v[c] >> (3 - b)) & 1;
                rgb_t col = gfx_hsv(HUE[c / 2], 230, on ? 255 : 35);
                gfx_rect(1 + c * 5, 4 + b * 6, 4, 4, col, 1);
            }
        break; }
    case 3: {                                                     /* big stacked */
        gfx_fill(0);
        gfx_7seg(3, 1, 12, 13, 3, hh / 10, RGB(255, 120, 40));
        gfx_7seg(17, 1, 12, 13, 3, hh % 10, RGB(255, 120, 40));
        gfx_7seg(3, 18, 12, 13, 3, tm.m / 10, RGB(60, 170, 255));
        gfx_7seg(17, 18, 12, 13, 3, tm.m % 10, RGB(60, 170, 255));
        gfx_rect(31, 31 - (tm.s * 32) / 60, 1, (tm.s * 32) / 60 + 1, RGB(40, 80, 40), 1);
        break; }
    case 4:                                                       /* digits over plasma */
        effects[0].frame(now);
        gfx_fade(70);
        digits_hhmm(10, 6, 11, 2, hh, tm.m, now, colon, 1);
        break;
    case 5:  draw_flip(hh, &tm, now, colon, show_date); break;
    case 6:  draw_nixie(hh, &tm, now, colon); break;
    case 7:  draw_rings(hh, &tm, now); break;
    case 8:  draw_sky(hh, &tm, now); break;
    case 9:  draw_pong(hh, &tm); break;
    default: clock2_draw(style % CLOCK_STYLES - 10u, &tm, hh, fmt24, show_date, now, ms); break;   /* clockface2.c */
    }
}
