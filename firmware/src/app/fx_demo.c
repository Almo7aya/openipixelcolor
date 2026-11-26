/* Demoscene-style effects: rotozoomer, twister, copper bars, shadebobs, kaleidoscope, Julia set, moire,
 * sunburst, spirograph, 3D cube, synthwave, voxel terrain, plasma globe, tartan.
 * Same rules as effects.c: integer maths only, all state in the shared scratch memory (one effect runs at a
 * time), frame(t) gets the time in ms and is called ~40x per second. Written for this project from the
 * well-known descriptions of these effects. */
#include "fx_demo.h"
#include "effects.h"
#include "fx_util.h"

typedef union {
    struct { uint8_t v[32][32]; } bob;                            /* shadebobs: heat per pixel */
    struct { uint32_t a, since; uint8_t set; } spiro;             /* spirograph */
    struct { uint32_t last; } cube;
    struct { int32_t u, v; uint32_t last; int16_t cam; } ter;     /* terrain camera, 1/16 world units */
    struct { uint8_t ang[5]; int8_t drift[5]; } globe;
} fx_t;

_Static_assert(sizeof(fx_t) <= FX_SCRATCH_BYTES, "fx_demo state does not fit the shared effect memory");
#define X (*(fx_t *)fx_scratch())

#define isqrt fx_isqrt
#define mix   fx_mix

/* ---- rotozoomer: a chequered plane that turns and zooms ------------------------------------- */
void rotozoom_frame(uint32_t t)
{
    uint8_t a = (uint8_t)(t / 30);
    int zoom = 330 + SN(t / 23) * 160 / 128;                     /* texels per pixel, 1/256: squares 5-12 px */
    int ca = CS(a) * zoom / 128, sa = SN(a) * zoom / 128;
    int ou = (int)((t * 5u) & 0xFFFFFFu), ov = (int)((t * 3u) & 0xFFFFFFu);   /* wraps on a whole tile */
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int dx = x * 2 - 31, dy = y * 2 - 31;
            int u = (dx * ca - dy * sa) / 2 + ou, v = (dx * sa + dy * ca) / 2 + ov;
            int tu = u >> 11, tv = v >> 11, fu = (u >> 8) & 7, fv = (v >> 8) & 7;
            uint8_t hue = (uint8_t)(tu * 24 + tv * 40 + (int)(t / 30));
            int edge = fu == 0 || fv == 0;
            rgb_t c = ((tu ^ tv) & 1) ? gfx_hsv(hue, 210, edge ? 110 : 255) : gfx_hsv((uint8_t)(hue + 128), 255, edge ? 25 : 60);
            gfx_px(x, y, c);
        }
}

/* ---- twister: a square column twisting and swaying ------------------------------------------ */
void twister_frame(uint32_t t)
{
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) gfx_px(x, y, RGB(3, 1, 8 + y / 3));
        uint8_t a = (uint8_t)(t / 14 + SN(y * 3 + t / 25) * 3 / 4);
        int cx = 16 * 16 + SN(y * 5 + t / 19) * 3 * 16 / 128;      /* 1/16 px */
        for (int i = 0; i < 4; i++) {
            int x1 = cx + SN(a + i * 64) * 9 * 16 / 128, x2 = cx + SN(a + (i + 1) * 64) * 9 * 16 / 128;
            if (x2 <= x1) continue;                                 /* this face looks away */
            int w = x2 - x1;
            rgb_t c = gfx_hsv((uint8_t)(i * 64 + t / 60), 200, (uint8_t)(50 + (w > 205 ? 205 : w)));
            int xa = (x1 + 8) >> 4, xb = (x2 + 8) >> 4;
            for (int x = xa; x < xb; x++) gfx_px(x, y, x == xb - 1 && xb - xa > 1 ? gfx_scale(c, 150) : c);
        }
    }
}

/* ---- copper bars: shiny bars circling up and down, the back ones dimmer --------------------- */
void copper_frame(uint32_t t)
{
    static const uint8_t HUE[7] = { 0, 18, 38, 85, 130, 165, 205 };
    gfx_fill(0);
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < 7; i++) {
            uint8_t a = (uint8_t)(t / 11 + i * 20);
            int front = CS(a) >= 0;
            if (front != pass) continue;
            int yc = 16 * 8 + SN(a) * 12 * 8 / 128;                  /* 1/8 px */
            for (int y = 0; y < 32; y++) {
                int d = y * 8 + 4 - yc;
                if (d < 0) d = -d;
                if (d >= 30) continue;
                int v = (255 - d * 255 / 30) * (front ? 255 : 140) / 255;
                for (int x = 0; x < 32; x++) {
                    int sheen = 215 + SN(x * 9 + y * 4 + t / 7) * 40 / 128;
                    gfx_px(x, y, gfx_hsv(HUE[i], (uint8_t)(d < 7 ? 110 : 255), (uint8_t)(v * sheen / 255)));
                }
            }
        }
}

/* ---- shadebobs: glowing discs that heat up the screen where they pass ------------------------ */
void bobs_init(void)
{
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) X.bob.v[y][x] = 0;
}

void bobs_frame(uint32_t t)
{
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) { uint8_t *p = &X.bob.v[y][x]; *p = *p > 3 ? (uint8_t)(*p - 3) : 0; }
    for (int i = 0; i < 4; i++) {
        int bx = 16 + SN(t / 17 + i * 50) * 12 / 128, by = 16 + SN(t / 23 + i * 80 + 64) * 12 / 128;
        for (int dy = -4; dy <= 4; dy++)
            for (int dx = -4; dx <= 4; dx++) {
                if (dx * dx + dy * dy > 17 || (unsigned)(bx + dx) > 31 || (unsigned)(by + dy) > 31) continue;
                uint8_t *p = &X.bob.v[by + dy][bx + dx];
                *p = *p > 243 ? 255 : (uint8_t)(*p + 12);
            }
    }
    uint8_t shift = (uint8_t)(t / 90);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            unsigned v = X.bob.v[y][x];
            gfx_px(x, y, gfx_hsv((uint8_t)(170 + v * 125 / 255 + shift), (uint8_t)(v > 200 ? 255 - (v - 200) * 3 : 255),
                                 (uint8_t)(v < 128 ? v * 2 : 255)));
        }
}

/* ---- kaleidoscope: a plasma folded into eight mirrored wedges ------------------------------- */
void kaleido_frame(uint32_t t)
{
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            uint8_t a = (uint8_t)(ang32[y][x] + t / 40);
            int r = dist32[y][x];
            int seg = a & 31;
            if (a & 32) seg = 31 - seg;
            int v = SN(seg * 6 + t / 9) + SN(r * 2 - (int)(t / 6)) + SN(seg * 3 + r + t / 13) + SN(r * seg / 8 - (int)(t / 11));
            gfx_px(x, y, gfx_hsv((uint8_t)(v / 4 + t / 30 + r / 2), 230, (uint8_t)(70 + (v + 512) * 185 / 1024)));
        }
}

/* ---- Julia set: the fractal for a point c going round a circle ------------------------------- */
void julia_frame(uint32_t t)
{
    /* c walks just outside the edge of the Mandelbrot set's main body (e^ia / 2 - e^2ia / 4): every
       Julia set along it is full of detail */
    uint8_t a = (uint8_t)(t / 70);
    int cr = (CS(a) * 2048 - CS(2 * a) * 1024) / 128 * 104 / 100, ci = (SN(a) * 2048 - SN(2 * a) * 1024) / 128 * 104 / 100;
    int scale = 380 + SN(t / 90) * 60 / 128;                       /* plane units per pixel */
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int zr = (x * 2 - 31) * scale / 2, zi = (y * 2 - 31) * scale / 2, n = 0;
            for (; n < 32; n++) {
                int r2 = (zr * zr) >> 12, i2 = (zi * zi) >> 12;
                if (r2 + i2 > (4 << 12)) break;
                zi = ((zr * zi) >> 11) + ci;
                zr = r2 - i2 + cr;
            }
            /* dark far away, bright along the edge of the set, black inside */
            gfx_px(x, y, n == 32 ? RGB(0, 0, 0) : gfx_hsv((uint8_t)(n * 14 + t / 25), 230, (uint8_t)(n * 22 > 255 ? 255 : n * 22)));
        }
}

/* ---- moire: two sets of rings over each other ------------------------------------------------ */
void moire_frame(uint32_t t)
{
    int x1 = 64 + SN(t / 21) * 56 / 128, y1 = 64 + SN(t / 27 + 64) * 56 / 128;   /* 1/4 px */
    int x2 = 64 + SN(t / 19 + 128) * 56 / 128, y2 = 64 + SN(t / 31 + 32) * 56 / 128;
    uint8_t hue = (uint8_t)(t / 50);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int ax = x * 4 + 2 - x1, ay = y * 4 + 2 - y1, bx = x * 4 + 2 - x2, by = y * 4 + 2 - y2;
            int r1 = (int)isqrt((unsigned)(ax * ax + ay * ay)), r2 = (int)isqrt((unsigned)(bx * bx + by * by));
            int v = SN(r1 * 11 - (int)(t / 8)) + SN(r2 * 11 - (int)(t / 8));   /* rings ~6 px apart, added */
            gfx_px(x, y, v >= 0 ? gfx_hsv(hue, 220, (uint8_t)(v > 254 ? 255 : v)) : gfx_hsv((uint8_t)(hue + 110), 230, (uint8_t)(-v > 254 ? 255 : -v)));
        }
}

/* ---- sunburst: turning rays round a glowing sun ---------------------------------------------- */
void sunburst_frame(uint32_t t)
{
    uint8_t h = (uint8_t)(t / 70);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int a = ang32[y][x], r = dist32[y][x];
            int ray = SN(a * 10 + r * 2 - (int)(t / 7));            /* 10 rays, slightly curved */
            rgb_t c = ray > 0 ? gfx_hsv(h, 240, (uint8_t)(120 + ray)) : gfx_hsv((uint8_t)(h + 22), 255, (uint8_t)(60 + ray / 3 + 40));
            if (r < 64) {                                           /* the sun: white core, warm glow */
                int k = r < 36 ? 256 : (64 - r) * 256 / 28;
                c = mix(c, r < 36 ? RGB(255, 250, 220) : gfx_hsv((uint8_t)(h + 10), 160, 255), k);
            }
            gfx_px(x, y, c);
        }
}

/* ---- spirograph: hypotrochoids drawn with a fading pen --------------------------------------- */
static const struct { uint8_t p, q, a, b; } SPIRO[6] = {
    { 7, 3, 9, 6 }, { 5, 2, 8, 7 }, { 11, 4, 10, 5 }, { 3, 1, 10, 5 }, { 13, 5, 9, 6 }, { 9, 4, 8, 7 },
};

void spiro_init(void) { X.spiro.a = 0; X.spiro.since = 0; X.spiro.set = 0; gfx_fill(0); }

static void spiro_at(uint32_t a, int *x, int *y)                  /* a: 1/4 of a sine-table step */
{
    unsigned s = X.spiro.set % 6u;
    uint8_t t1 = (uint8_t)(a >> 2), t2 = (uint8_t)((a * SPIRO[s].p / SPIRO[s].q) >> 2);
    *x = 16 + (SPIRO[s].a * CS(t1) + SPIRO[s].b * CS(t2)) / 128;
    *y = 16 + (SPIRO[s].a * SN(t1) - SPIRO[s].b * SN(t2)) / 128;
}

void spiro_frame(uint32_t t)
{
    unsigned s = X.spiro.set % 6u, turns = SPIRO[s].q;
    uint32_t done = X.spiro.a;
    gfx_fade(done > turns * 1024u ? 200 : 252);                   /* drawn: let it fade, then the next */
    if (done > turns * 1024u + 400u) { X.spiro.set++; X.spiro.a = 0; return; }
    for (int k = 0; k < 6; k++) {
        int xa, ya, xb, yb;
        spiro_at(X.spiro.a, &xa, &ya);
        X.spiro.a += 3;
        spiro_at(X.spiro.a, &xb, &yb);
        if (X.spiro.a <= turns * 1024u) gfx_line(xa, ya, xb, yb, gfx_hsv((uint8_t)(X.spiro.a / 12 + t / 40), 220, 255));
    }
}

/* ---- 3D cube: a wireframe cube turning in space ---------------------------------------------- */
void cube_init(void) { gfx_fill(0); }

void cube_frame(uint32_t t)
{
    static const int8_t V[8][3] = { {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1} };
    static const uint8_t E[12][2] = { {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };
    gfx_fade(120);
    uint8_t ax = (uint8_t)(t / 23), ay = (uint8_t)(t / 17), az = (uint8_t)(t / 31);
    int sx[8], sy[8], sz[8];
    for (int i = 0; i < 8; i++) {
        int x = V[i][0] * 60, y = V[i][1] * 60, z = V[i][2] * 60, n;
        n = (y * CS(ax) - z * SN(ax)) / 128; z = (y * SN(ax) + z * CS(ax)) / 128; y = n;
        n = (x * CS(ay) + z * SN(ay)) / 128; z = (-x * SN(ay) + z * CS(ay)) / 128; x = n;
        n = (x * CS(az) - y * SN(az)) / 128; y = (x * SN(az) + y * CS(az)) / 128; x = n;
        int zz = z + 260;
        sx[i] = 16 + x * 30 / zz; sy[i] = 16 + y * 30 / zz; sz[i] = z;
    }
    for (int e = 0; e < 12; e++) {
        int a = E[e][0], b = E[e][1], depth = (sz[a] + sz[b]) / 2;      /* -104..104: near is negative */
        uint8_t v = (uint8_t)(150 - depth * 100 / 110);
        gfx_line(sx[a], sy[a], sx[b], sy[b], gfx_hsv((uint8_t)(e * 21 + t / 30), 200, v));
    }
    for (int i = 0; i < 8; i++) if (sz[i] < 0) gfx_px(sx[i], sy[i], RGB(255, 255, 255));
}

/* ---- synthwave: a striped sun setting behind a neon grid ------------------------------------- */
void synth_frame(uint32_t t)
{
    for (int y = 0; y < 17; y++) {                                  /* sky */
        rgb_t c = mix(RGB(14, 0, 34), RGB(120, 20, 90), y * 256 / 16);
        for (int x = 0; x < 32; x++) {
            unsigned h = (unsigned)(x * 97 + y * 57) * 2654435761u;
            gfx_px(x, y, (y < 9 && (h >> 27) == 0) ? gfx_scale(RGB(200, 200, 255), sin8[(uint8_t)(t / 9 + (h >> 8))]) : c);
        }
    }
    for (int y = 3; y < 17; y++)                                    /* sun with gaps sliding down */
        for (int x = 7; x < 25; x++) {
            int dx = x * 2 - 31, dy = (y - 12) * 2 - 1;
            if (dx * dx + dy * dy > 18 * 18) continue;
            if (y > 9 && ((y * 4 + (int)(t / 60)) % 12) < 4 - (16 - y) / 4) continue;
            gfx_px(x, y, mix(RGB(255, 230, 80), RGB(255, 40, 140), (y - 3) * 256 / 14));
        }
    for (int y = 17; y < 32; y++) for (int x = 0; x < 32; x++) gfx_px(x, y, RGB(12, 0, 22 + (y - 17)));
    gfx_rect(0, 17, 32, 1, RGB(255, 60, 200), 1);                     /* horizon */
    int off = (int)(t / 5) % 32;
    for (int k = 0; k < 12; k++) {                                   /* lines across, coming closer */
        int z = k * 32 + 32 - off;
        if (z <= 0) continue;
        int y = 17 + 256 / z;
        if (y > 31) continue;
        gfx_rect(0, y, 32, 1, gfx_scale(RGB(255, 40, 220), (uint8_t)(y < 20 ? 110 : 90 + (y - 17) * 11)), 1);
    }
    for (int i = -8; i <= 8; i++)                                    /* lines to the horizon */
        gfx_line(16 + i * 2, 18, 16 + i * 15, 31, RGB(150, 30, 200));
}

/* ---- terrain: flying over hills, lakes and snowy peaks (voxel landscape) --------------------- */
static int ter_h(int u, int v)                                       /* world units: 1/16 tile; 0..255 */
{
    int h = 120 + (SN(u * 3 >> 4) * 50 + SN((v * 2 >> 4) + 40) * 44 + SN((u + v) * 5 >> 4) * 22 + SN((u * 7 - v * 4) >> 4) * 12) / 128;
    return h < 90 ? 90 : h;                                          /* water */
}

void terrain_init(void) { X.ter.u = 0; X.ter.v = 0; X.ter.last = 0; X.ter.cam = 0; }

void terrain_frame(uint32_t t)
{
    uint32_t dt = X.ter.last ? t - X.ter.last : 25;
    if (dt > 100) dt = 100;
    X.ter.last = t;
    uint8_t yaw = (uint8_t)(SN(t / 160) / 3);
    int du = SN(yaw), dv = CS(yaw);                                  /* heading, 1/128 */
    X.ter.u += du * (int)dt / 25; X.ter.v += dv * (int)dt / 25;      /* ~ 8 world units a frame */
    int cu = X.ter.u >> 4, cv = X.ter.v >> 4;
    int ground = ter_h(cu, cv), ahead = ter_h(cu + du * 16 / 4, cv + dv * 16 / 4);   /* climb over what's coming */
    int want = (ground > ahead ? ground : ahead) + 32;
    if (want < 150 + SN(t / 70) * 12 / 128) want = 150 + SN(t / 70) * 12 / 128;
    X.ter.cam = (int16_t)(X.ter.cam ? X.ter.cam + (want - X.ter.cam) / 8 : want);
    int cam = X.ter.cam;
    for (int x = 0; x < 32; x++) {
        int ru = du + CS(yaw) * (x - 16) / 18, rv = dv - SN(yaw) * (x - 16) / 18;   /* ray direction, 1/128 */
        int top = 32;
        for (int y = 0; y < 32; y++) gfx_px(x, y, mix(RGB(40, 90, 200), RGB(200, 150, 190), y >= 14 ? 256 : y * 256 / 14));
        for (int z = 2; z < 64 && top > 0; z += 1 + z / 12) {
            int h = ter_h(cu + ru * z / 4, cv + rv * z / 4);         /* ~ 2000 world units deep */
            int sy = 9 + (cam - h) * 4 / z;
            if (sy >= top) continue;
            rgb_t c = h <= 90 ? gfx_hsv(150, 220, (uint8_t)(150 + SN(z * 40 + t / 6) / 4))
                    : h < 100 ? RGB(200, 180, 110)
                    : h < 160 ? gfx_hsv((uint8_t)(70 + (h - 100) / 8), 220, (uint8_t)(90 + (h - 100)))
                    : h < 200 ? RGB(110 + (h - 160), 95 + (h - 160) / 2, 80)
                    : RGB(235, 240, 255);
            c = mix(c, RGB(200, 150, 190), z * 256 / 70);             /* haze far away */
            for (int y = sy < 0 ? 0 : sy; y < top; y++) gfx_px(x, y, c);
            top = sy < 0 ? 0 : sy;
        }
    }
}

/* ---- plasma globe: lightning from the core to the glass -------------------------------------- */
void globe_init(void)
{
    for (int i = 0; i < 5; i++) { X.globe.ang[i] = (uint8_t)(i * 51 + gfx_rnd(30)); X.globe.drift[i] = (int8_t)((int)gfx_rnd(5) - 2); }
    gfx_fill(0);
}

void globe_frame(uint32_t t)
{
    gfx_fade(90);
    for (int y = 0; y < 32; y++)                                     /* the glass */
        for (int x = 0; x < 32; x++) {
            int d = dist32[y][x];
            if (d > 112 && d < 124) gfx_px_add(x, y, RGB(14, 4, 26));
        }
    for (int i = 0; i < 5; i++) {
        if (gfx_rnd(20) == 0) X.globe.drift[i] = (int8_t)((int)gfx_rnd(5) - 2);
        X.globe.ang[i] = (uint8_t)(X.globe.ang[i] + X.globe.drift[i]);
        int a = X.globe.ang[i] * 4, px = 16 * 4, py = 16 * 4;       /* 1/4 px */
        for (int r = 2; r <= 14; r++) {
            a += (int)gfx_rnd(41) - 20;
            int target = X.globe.ang[i] * 4;                          /* pulled back toward its direction */
            a += (target - a) / 4;
            uint8_t aa = (uint8_t)(a >> 2);
            int nx = 64 + CS(aa) * r * 4 / 128, ny = 64 + SN(aa) * r * 4 / 128;
            gfx_line(px >> 2, py >> 2, nx >> 2, ny >> 2, gfx_hsv((uint8_t)(200 + r * 3), (uint8_t)(120 + r * 8), (uint8_t)(255 - r * 6)));
            px = nx; py = ny;
        }
        gfx_px_add(px >> 2, py >> 2, RGB(180, 120, 255));            /* where it touches the glass */
    }
    int pulse = 200 + SN(t / 3) / 3;
    gfx_circle(16, 16, 2, gfx_scale(RGB(255, 150, 255), (uint8_t)pulse), 1);
    gfx_px(16, 16, RGB(255, 255, 255)); gfx_px(15, 15, RGB(255, 230, 255));
}

/* ---- tartan: woven plaid drifting slowly ----------------------------------------------------- */
static const rgb_t SETT[3][4] = {
    { RGB(150, 10, 20),  RGB(10, 20, 70),  RGB(10, 70, 30),  RGB(220, 200, 60) },
    { RGB(10, 50, 110),  RGB(10, 80, 50),  RGB(5, 5, 20),    RGB(160, 160, 170) },
    { RGB(90, 20, 110),  RGB(200, 200, 210), RGB(10, 90, 100), RGB(40, 10, 60) },
};
static const uint8_t SW[8] = { 0, 0, 0, 0, 1, 1, 3, 2 };            /* stripe of each 2-px step (+mirror) */

static rgb_t sett_at(const rgb_t *p, int i)
{
    i &= 31;
    if (i >= 16) i = 31 - i;                                         /* the sett mirrors */
    return p[SW[i / 2]];
}

void tartan_frame(uint32_t t)
{
    const rgb_t *p = SETT[(t / 15000) % 3];
    int s = (int)(t / 150);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int warp = ((x + y + s) >> 1) & 1;                       /* 2/2 twill: diagonal weave */
            rgb_t c = warp ? sett_at(p, x + s) : sett_at(p, y + s / 2);
            gfx_px(x, y, gfx_scale(c, (uint8_t)(((x + y) & 1) ? 255 : 205)));
        }
}
