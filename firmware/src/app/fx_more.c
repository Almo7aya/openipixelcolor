/* More effects: aurora, lava lamp, rain with lightning, DNA helix, warp speed, radar.
 * Same rules as effects.c: integer maths only, all state in the shared scratch memory (one effect runs
 * at a time), frame(t) gets the time in ms and is called ~40x per second. */
#include "fx_more.h"
#include "effects.h"
#include "gfx.h"
#include "tables.h"

typedef struct { int16_t x, y; uint8_t speed; } drop_t;
typedef struct { int8_t x; uint8_t age; } splash_t;
typedef struct { uint16_t r; uint8_t ang, speed; } wstar_t;
typedef struct { int8_t x, y; uint8_t glow, life; } blip_t;

#define NDROP  44
#define NSPL   16
#define NWARP  72
#define NBLIP  6

typedef union {
    struct { drop_t d[NDROP]; splash_t sp[NSPL]; uint32_t next_bolt, bolt_t; int8_t bolt[32]; uint8_t inited; } rain;
    struct { wstar_t s[NWARP]; } warp;
    struct { blip_t b[NBLIP]; } radar;
} fx_t;

_Static_assert(sizeof(fx_t) <= FX_SCRATCH_BYTES, "fx_more state does not fit the shared effect memory");
#define X (*(fx_t *)fx_scratch())

void fx_nop_init(void) { }

/* ---- aurora: two curtains of light over a starry sky ---------------------------------------- */
void aurora_frame(uint32_t t)
{
    for (int x = 0; x < 32; x++) {
        int y1 = 8 + ((int)sin8[(uint8_t)(x * 7 + t / 25)] - 128) * 4 / 127 + ((int)sin8[(uint8_t)(x * 13 - t / 41)] - 128) * 3 / 127;
        int y2 = 15 + ((int)sin8[(uint8_t)(x * 5 - t / 33 + 90)] - 128) * 3 / 127;
        uint8_t hue = (uint8_t)(92 + ((int)sin8[(uint8_t)(x * 5 + t / 60)] - 128) * 30 / 127);
        int shim = 150 + sin8[(uint8_t)(x * 31 + t / 6)] * 105 / 255;
        for (int y = 0; y < 32; y++) {
            int d1 = y - y1, d2 = y - y2;
            int v1 = d1 < 0 ? 255 + d1 * 80 : 255 - d1 * 15;
            int v2 = d2 < 0 ? 180 + d2 * 70 : 180 - d2 * 18;
            if (v1 < 0) v1 = 0;
            if (v2 < 0) v2 = 0;
            rgb_t c = 0;
            if (v1) c = gfx_hsv((uint8_t)(hue + (d1 > 0 ? d1 * 4 : 0)), 230, (uint8_t)(v1 * shim / 255));
            if (v2) { rgb_t c2 = gfx_hsv((uint8_t)(hue + 70 + d2 * 3), 220, (uint8_t)(v2 * shim / 400));
                      c = RGB((((c >> 16) & 255) + ((c2 >> 16) & 255)) > 255 ? 255 : ((c >> 16) & 255) + ((c2 >> 16) & 255),
                              (((c >> 8) & 255) + ((c2 >> 8) & 255)) > 255 ? 255 : ((c >> 8) & 255) + ((c2 >> 8) & 255),
                              ((c & 255) + (c2 & 255)) > 255 ? 255 : (c & 255) + (c2 & 255)); }
            unsigned h = (unsigned)(x * 73 + y * 151) * 2654435761u;
            if (!c && (h >> 26) == 0) c = gfx_scale(RGB(180, 180, 220), sin8[(uint8_t)(t / 11 + (h >> 8))]);
            if (y >= 29 + ((int)sin8[(uint8_t)(x * 11)] - 128) / 90) c = 0;                 /* skyline */
            gfx_px(x, y, c);
        }
    }
}

/* ---- lava lamp: five metaballs ---------------------------------------------------------------- */
void lava_frame(uint32_t t)
{
    static const uint8_t SPX[5] = { 11, 17, 7, 23, 13 }, SPY[5] = { 19, 13, 23, 9, 29 }, PH[5] = { 0, 60, 120, 180, 220 };
    static const uint8_t R2[5] = { 36, 30, 25, 42, 20 };
    int bx[5], by[5];
    for (int i = 0; i < 5; i++) {
        bx[i] = 16 * 8 + ((int)sin8[(uint8_t)(t * SPX[i] / 700 + PH[i])] - 128) * 9 * 8 / 127;
        by[i] = 16 * 8 + ((int)sin8[(uint8_t)(t * SPY[i] / 900 + PH[i] * 3)] - 128) * 13 * 8 / 127;
    }
    uint8_t warm = (uint8_t)(244 + ((int)sin8[(uint8_t)(t / 160)] - 128) * 10 / 127);  /* magenta-red .. red */
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int f = 0;
            for (int i = 0; i < 5; i++) {
                int dx = x * 8 + 4 - bx[i], dy = y * 8 + 4 - by[i];
                f += R2[i] * 4096 / ((dx * dx + dy * dy) / 16 + 4);
            }
            rgb_t c;
            if (f < 700)       c = RGB(22 + y / 2, 0, 30 + y);                                      /* liquid */
            else if (f < 1000) c = gfx_hsv(warm, 255, (uint8_t)(70 + (f - 700) / 3));             /* soft edge */
            else {                                                                                /* wax: red rim, yellow core */
                int k = f > 5000 ? 40 : (f - 1000) / 100;
                c = gfx_hsv((uint8_t)(warm + 12 + k), (uint8_t)(255 - k * 2), 255);
            }
            gfx_px(x, y, c);
        }
}

/* ---- rain with splashes and lightning ------------------------------------------------------- */
static void drop_reset(drop_t *d, int anywhere)
{
    d->x = (int16_t)((int)gfx_rnd(40) << 8);
    d->y = (int16_t)(anywhere ? (int)gfx_rnd(32) << 8 : -((int)gfx_rnd(16) << 8));
    d->speed = (uint8_t)(130 + gfx_rnd(90));
}

void rain_init(void)
{
    for (int i = 0; i < NDROP; i++) drop_reset(&X.rain.d[i], 1);
    for (int i = 0; i < NSPL; i++) X.rain.sp[i].age = 255;
    X.rain.inited = 0;
}

void rain_frame(uint32_t t)
{
    if (!X.rain.inited) { X.rain.inited = 1; X.rain.next_bolt = t + 1500; X.rain.bolt_t = 0; }
    if (t >= X.rain.next_bolt) {                             /* new lightning bolt */
        X.rain.bolt_t = t;
        X.rain.next_bolt = t + 2500 + gfx_rnd(5000);
        int x = 6 + (int)gfx_rnd(20);
        for (int y = 0; y < 32; y++) { x += (int)gfx_rnd(3) - 1; if (gfx_rnd(5) == 0) x += (int)gfx_rnd(3) - 1; X.rain.bolt[y] = (int8_t)x; }
    }
    uint32_t bt = t - X.rain.bolt_t;
    int flash = 0;
    if (X.rain.bolt_t && bt < 60) flash = 200; else if (X.rain.bolt_t && bt >= 110 && bt < 160) flash = 140;
    else if (X.rain.bolt_t && bt < 600) flash = (int)(600 - bt) / 12;

    for (int y = 0; y < 32; y++) {                           /* sky and clouds */
        int base = 6 + y / 3;
        for (int x = 0; x < 32; x++) {
            int cl = y < 7 ? ((int)sin8[(uint8_t)(x * 9 + t / 70)] + (int)sin8[(uint8_t)(x * 17 - t / 50 + y * 30)]) / 16 - y * 3 : 0;
            if (cl < 0) cl = 0;
            int r = base / 2 + cl + flash * 3 / 5, g = base / 2 + cl + flash * 3 / 5, b = base + 8 + cl + flash * 4 / 5;
            gfx_px(x, y, RGB(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b));
        }
    }
    if (X.rain.bolt_t && (bt < 60 || (bt >= 110 && bt < 160)))
        for (int y = 3; y < 31; y++) { gfx_px(X.rain.bolt[y], y, RGB(255, 255, 255)); gfx_px_add(X.rain.bolt[y] + 1, y, RGB(90, 70, 160)); }

    for (int i = 0; i < NDROP; i++) {                        /* drops, slanted by the wind */
        drop_t *d = &X.rain.d[i];
        d->y = (int16_t)(d->y + d->speed);
        d->x = (int16_t)(d->x - d->speed / 4);
        int x = d->x >> 8, y = d->y >> 8;
        if (y >= 30) {
            for (int k = 0; k < NSPL; k++) if (X.rain.sp[k].age == 255) { X.rain.sp[k].x = (int8_t)x; X.rain.sp[k].age = 0; break; }
            drop_reset(d, 0);
            continue;
        }
        gfx_px(x, y, RGB(140, 170, 230));
        gfx_px_add(x + 1, y - 1, RGB(50, 65, 100));
        gfx_px_add(x + 2, y - 2, RGB(25, 30, 50));
    }
    gfx_rect(0, 31, 32, 1, RGB(10, 14, 30 + flash / 3), 1);  /* puddle */
    for (int k = 0; k < NSPL; k++) {
        splash_t *s = &X.rain.sp[k];
        if (s->age == 255) continue;
        int a = s->age++;
        if (a > 5) { s->age = 255; continue; }
        gfx_px(s->x - 1 - a / 2, 30 - (a < 3 ? 1 : 0), RGB(90, 120, 180));
        gfx_px(s->x + 1 + a / 2, 30 - (a < 3 ? 1 : 0), RGB(90, 120, 180));
    }
}

/* ---- DNA double helix ------------------------------------------------------------------------ */
void dna_frame(uint32_t t)
{
    static const rgb_t PAIR[4][2] = {
        { RGB(255, 70, 70),  RGB(70, 220, 90) },
        { RGB(70, 140, 255), RGB(255, 210, 60) },
        { RGB(70, 220, 90),  RGB(255, 70, 70) },
        { RGB(255, 210, 60), RGB(70, 140, 255) },
    };
    gfx_fill(0);
    for (int y = 0; y < 32; y++) {
        uint8_t a = (uint8_t)(y * 11 + t / 7);
        int s = (int)sin8[a] - 128, c = (int)cos8(a) - 128;
        int x1 = 16 + s * 10 / 127, x2 = 16 - s * 10 / 127;
        uint8_t v1 = (uint8_t)(150 + c * 105 / 127), v2 = (uint8_t)(150 - c * 105 / 127);
        if (((y + (int)(t / 120)) & 3) == 0) {               /* base pair rung */
            const rgb_t *pp = PAIR[((y + (int)(t / 120)) >> 2) & 3];
            int lo = x1 < x2 ? x1 : x2, hi = x1 < x2 ? x2 : x1, mid = (lo + hi) / 2;
            for (int x = lo + 1; x < hi; x++) gfx_px(x, y, gfx_scale(x <= mid ? pp[0] : pp[1], 130));
        }
        rgb_t s1 = gfx_scale(RGB(80, 200, 255), v1), s2 = gfx_scale(RGB(255, 80, 200), v2);
        if (c >= 0) { gfx_px(x2, y, s2); gfx_px(x1, y, s1); }          /* front strand drawn last */
        else        { gfx_px(x1, y, s1); gfx_px(x2, y, s2); }
    }
}

/* ---- warp speed: streaks rush out of the centre, with jumps --------------------------------- */
static void warp_reset(wstar_t *s, int anywhere)
{
    s->ang = (uint8_t)gfx_rand();
    s->r = (uint16_t)(anywhere ? gfx_rnd(16 << 8) : gfx_rnd(3 << 8));
    s->speed = (uint8_t)(6 + gfx_rnd(10));
}

void warp_init(void) { for (int i = 0; i < NWARP; i++) warp_reset(&X.warp.s[i], 1); }

void warp_frame(uint32_t t)
{
    unsigned phase = t % 7000u;
    unsigned boost = phase < 4500 ? 16 : phase < 5500 ? 16 + (phase - 4500) / 12 : 100 - (phase - 5500) * 84 / 1500;
    gfx_fade(phase >= 4500 && phase < 6000 ? 200 : 120);
    for (int i = 0; i < NWARP; i++) {
        wstar_t *s = &X.warp.s[i];
        unsigned r0 = s->r;
        unsigned step = ((r0 >> 3) + 40) * s->speed * boost / 256;
        s->r = (uint16_t)(r0 + step);
        if ((s->r >> 8) > 24) { warp_reset(s, 0); continue; }
        int cs = (int)cos8(s->ang) - 128, sn = (int)sin8[s->ang] - 128;
        int xa = 16 + cs * (int)(r0 >> 8) / 127, ya = 16 + sn * (int)(r0 >> 8) / 127;
        int xb = 16 + cs * (int)(s->r >> 8) / 127, yb = 16 + sn * (int)(s->r >> 8) / 127;
        unsigned v = 60 + (s->r >> 8) * 9;
        gfx_line(xa, ya, xb, yb, gfx_scale(RGB(170, 200, 255), (uint8_t)(v > 255 ? 255 : v)));
        gfx_px(xb, yb, RGB(255, 255, 255));
    }
    gfx_px_add(15, 15, RGB(40, 60, 120)); gfx_px_add(16, 16, RGB(40, 60, 120));
}

/* ---- radar: sweep with afterglow, range rings, blips ---------------------------------------- */
static void blip_reset(blip_t *b)
{
    int r = 4 + (int)gfx_rnd(11);
    uint8_t a = (uint8_t)gfx_rand();
    b->x = (int8_t)(16 + ((int)cos8(a) - 128) * r / 127);
    b->y = (int8_t)(16 + ((int)sin8[a] - 128) * r / 127);
    b->glow = 0;
    b->life = (uint8_t)(3 + gfx_rnd(5));                     /* sweeps until it moves elsewhere */
}

void radar_init(void) { for (int i = 0; i < NBLIP; i++) blip_reset(&X.radar.b[i]); }

void radar_frame(uint32_t t)
{
    uint8_t sweep = (uint8_t)(t / 9);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            unsigned d = dist32[y][x];
            if (d > 126) { gfx_px(x, y, 0); continue; }
            unsigned ago = (uint8_t)(sweep - ang32[y][x]);
            int g = ago < 70 ? 200 - (int)ago * 200 / 70 : 0;
            if (ago < 2) g = 255;
            int ring = (d % 40u) < 4u || d > 120;
            int cross = x == 15 || x == 16 || y == 15 || y == 16;
            int base = ring ? 45 : cross ? 22 : 6;
            int gg = g + base > 255 ? 255 : g + base;
            gfx_px(x, y, RGB(ago < 2 ? 120 : 0, gg, ago < 2 ? 100 : g / 6));
        }
    for (int i = 0; i < NBLIP; i++) {
        blip_t *b = &X.radar.b[i];
        unsigned ago = (uint8_t)(sweep - ang32[(uint8_t)b->y & 31][(uint8_t)b->x & 31]);
        if (ago < 4 && b->glow < 200) {
            b->glow = 255;
            if (b->life-- == 0) blip_reset(b);
        }
        if (b->glow) {
            gfx_px(b->x, b->y, RGB(b->glow, 255, b->glow));
            gfx_px_add(b->x + 1, b->y, RGB(0, b->glow / 3, 0)); gfx_px_add(b->x - 1, b->y, RGB(0, b->glow / 3, 0));
            gfx_px_add(b->x, b->y + 1, RGB(0, b->glow / 3, 0)); gfx_px_add(b->x, b->y - 1, RGB(0, b->glow / 3, 0));
            b->glow = b->glow > 4 ? (uint8_t)(b->glow - 4) : 0;
        }
    }
}
