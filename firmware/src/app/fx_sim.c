/* Little worlds and simulations: ocean, falling sand, Langton's ants, a flock (boids), a galaxy, snowfall,
 * an aquarium, fireflies, a maze that builds and solves itself, sorting, a pendulum wave, a self-playing
 * brick breaker and a meadow.
 * Same rules as effects.c: integer maths only, all state in the shared scratch memory (one effect runs at a
 * time), frame(t) gets the time in ms and is called ~40x per second. */
#include "fx_sim.h"
#include "effects.h"
#include "fx_util.h"

#define NBOID 24
#define NSNOW 40
#define NFLY  12
#define NFISH 4
#define NBUB  6

typedef struct { int16_t x, y, vx, vy; } boid_t;                 /* 1/256 px */
typedef struct { int16_t x, y; uint8_t sp, near; } flake_t;
typedef struct { int16_t x; uint8_t y, dir, sp, hue, ph; } fish_t;   /* x in 1/16 px */
typedef struct { int16_t y; uint8_t x, ph; } bubble_t;
typedef struct { int16_t x, y, vx, vy; uint8_t ph, per; } fly_t;

typedef union {
    struct { uint8_t g[32][32]; uint16_t n; uint8_t drain, hue; uint32_t since; } sand;
    struct { uint8_t g[32][32]; struct { uint8_t x, y, d; } a[3]; uint16_t frames; } ant;
    struct { boid_t b[NBOID]; } flock;
    struct { flake_t f[NSNOW]; uint16_t pile[32]; uint8_t melt; } snow;       /* pile: 1/16 px */
    struct { fish_t f[NFISH]; bubble_t b[NBUB]; } aqua;
    struct { fly_t f[NFLY]; } fly;
    struct { uint8_t g[31][31], dist[225], stk[225], q[225]; uint16_t sp, qh, qt; uint8_t phase, layer, cur; uint32_t t0; } maze;
    struct { uint8_t a[32], algo, phase, i, j, k, lo, hi, dir, hi1, hi2; uint16_t wait; } sort;
    struct { uint8_t row[5]; int16_t bx, by, vx, vy, px; uint8_t wait; } brk;
} fx_t;

_Static_assert(sizeof(fx_t) <= FX_SCRATCH_BYTES, "fx_sim state does not fit the shared effect memory");
#define X (*(fx_t *)fx_scratch())

/* ---- ocean: the sea seen from above, crests catching the light ------------------------------- */
void ocean_frame(uint32_t t)
{
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int w1 = SN(x * 9 + y * 4 + (int)(t / 9));
            int w2 = SN(x * 5 - y * 7 + (int)(t / 13) + 60);
            int w3 = SN(y * 11 + (int)(t / 7) + SN(x * 6 + (int)(t / 17)) / 4);
            int v = w1 + w2 + w3, k = v + 384;                         /* 0..765 */
            rgb_t c = RGB(0, 35 + k / 7, 80 + k / 5);
            if (v > 150) c = fx_mix(c, RGB(180, 240, 255), (v - 150) * 256 / 234);
            gfx_px(x, y, c);
        }
}

/* ---- falling sand: a moving spout fills the box, then the floor opens ------------------------ */
void sand_init(void)
{
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) X.sand.g[y][x] = 0;
    X.sand.n = 0; X.sand.drain = 0; X.sand.hue = (uint8_t)gfx_rand(); X.sand.since = 0;
}

void sand_frame(uint32_t t)
{
    if (!X.sand.since) X.sand.since = t | 1u;
    if (!X.sand.drain) {
        if (X.sand.n > 600 || t - X.sand.since > 45000u) X.sand.drain = 1;
        else {
            int sx = 15 + SN(t / 40) * 13 / 128;
            uint8_t v = (uint8_t)((t / 45 + X.sand.hue) | 1u);        /* never 0 (= empty); layers of colour */
            for (int k = 0; k < 2; k++) if (!X.sand.g[0][sx + k]) { X.sand.g[0][sx + k] = v; X.sand.n++; }
        }
    }
    int ltr = (int)((t / 25) & 1);
    for (int y = 30; y >= 0; y--)
        for (int i = 0; i < 32; i++) {
            int x = ltr ? i : 31 - i;
            uint8_t v = X.sand.g[y][x];
            if (!v) continue;
            int d = (gfx_rand() & 1) ? 1 : -1, nx = -1;
            if (!X.sand.g[y + 1][x]) nx = x;
            else if ((unsigned)(x + d) < 32 && !X.sand.g[y + 1][x + d]) nx = x + d;
            else if ((unsigned)(x - d) < 32 && !X.sand.g[y + 1][x - d]) nx = x - d;
            if (nx >= 0) { X.sand.g[y + 1][nx] = v; X.sand.g[y][x] = 0; }
        }
    if (X.sand.drain && (t / 25) % 2 == 0) {                          /* the floor is open */
        for (int x = 0; x < 32; x++) if (X.sand.g[31][x]) { X.sand.g[31][x] = 0; X.sand.n--; }
        if (!X.sand.n) { X.sand.drain = 0; X.sand.since = t | 1u; X.sand.hue = (uint8_t)(X.sand.hue + 85); }
    }
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            uint8_t v = X.sand.g[y][x];
            gfx_px(x, y, v ? gfx_hsv(v, 190, (uint8_t)(175 + ((x * 7 + y * 13 + v) % 5) * 20)) : RGB(3, 3, 9));
        }
}

/* ---- Langton's ants: three ants turning on the colours they leave ---------------------------- */
void ants_init(void)
{
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) X.ant.g[y][x] = 0;
    for (int i = 0; i < 3; i++) { X.ant.a[i].x = (uint8_t)(8 + i * 8); X.ant.a[i].y = (uint8_t)(12 + i * 4); X.ant.a[i].d = (uint8_t)i; }
    X.ant.frames = 0;
}

void ants_frame(uint32_t t)
{
    static const int8_t DX[4] = { 0, 1, 0, -1 }, DY[4] = { -1, 0, 1, 0 };
    static const uint8_t HUE[3] = { 0, 90, 165 };
    (void)t;
    if (++X.ant.frames > 3200) ants_init();                          /* ~80 s, then a fresh start */
    for (int s = 0; s < 5; s++)
        for (int i = 0; i < 3; i++) {
            uint8_t *c = &X.ant.g[X.ant.a[i].y][X.ant.a[i].x];
            if (!*c) { X.ant.a[i].d = (uint8_t)((X.ant.a[i].d + 1) & 3); *c = (uint8_t)(i + 1); }
            else     { X.ant.a[i].d = (uint8_t)((X.ant.a[i].d + 3) & 3); *c = 0; }
            X.ant.a[i].x = (uint8_t)((X.ant.a[i].x + DX[X.ant.a[i].d]) & 31);
            X.ant.a[i].y = (uint8_t)((X.ant.a[i].y + DY[X.ant.a[i].d]) & 31);
        }
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            uint8_t v = X.ant.g[y][x];
            gfx_px(x, y, v ? gfx_hsv(HUE[v - 1], 220, 150) : RGB(2, 2, 5));
        }
    for (int i = 0; i < 3; i++) gfx_px(X.ant.a[i].x, X.ant.a[i].y, RGB(255, 255, 255));
}

/* ---- flock: boids keeping together, lining up, keeping apart ---------------------------------- */
void flock_init(void)
{
    for (int i = 0; i < NBOID; i++) {
        X.flock.b[i].x = (int16_t)gfx_rnd(8192); X.flock.b[i].y = (int16_t)gfx_rnd(8192);
        X.flock.b[i].vx = (int16_t)((int)gfx_rnd(300) - 150); X.flock.b[i].vy = (int16_t)((int)gfx_rnd(300) - 150);
    }
    gfx_fill(0);
}

static int wrapd(int d) { return d > 4096 ? d - 8192 : d < -4096 ? d + 8192 : d; }

void flock_frame(uint32_t t)
{
    static const uint8_t HUE[3] = { 140, 200, 20 };
    gfx_fade(185);
    int tx = 16 * 256 + SN(t / 60) * 10 * 256 / 128, ty = 16 * 256 + SN(t / 83 + 64) * 10 * 256 / 128;   /* a wandering lure */
    for (int i = 0; i < NBOID; i++) {
        boid_t *b = &X.flock.b[i];
        int cx = 0, cy = 0, ax = 0, ay = 0, sx = 0, sy = 0, n = 0;
        for (int j = 0; j < NBOID; j++) {
            if (j == i) continue;
            int dx = wrapd(X.flock.b[j].x - b->x), dy = wrapd(X.flock.b[j].y - b->y);
            int d2 = (dx >> 4) * (dx >> 4) + (dy >> 4) * (dy >> 4);   /* (1/16 px)^2 */
            if (d2 > 96 * 96) continue;                              /* sees 6 px */
            n++; cx += dx; cy += dy; ax += X.flock.b[j].vx; ay += X.flock.b[j].vy;
            if (d2 < 40 * 40) { sx -= dx; sy -= dy; }                /* too close: 2.5 px */
        }
        int vx = b->vx, vy = b->vy;
        if (n) { vx += cx / n / 32 + (ax / n - vx) / 10; vy += cy / n / 32 + (ay / n - vy) / 10; }
        vx += sx / 20 + wrapd(tx - b->x) / 250; vy += sy / 20 + wrapd(ty - b->y) / 250;
        int sp = (int)fx_isqrt((unsigned)(vx * vx + vy * vy));
        if (sp > 260) { vx = vx * 260 / sp; vy = vy * 260 / sp; }
        else if (sp < 110 && sp > 0) { vx = vx * 110 / sp; vy = vy * 110 / sp; }
        b->vx = (int16_t)vx; b->vy = (int16_t)vy;
    }
    for (int i = 0; i < NBOID; i++) {
        boid_t *b = &X.flock.b[i];
        b->x = (int16_t)((b->x + b->vx) & 8191); b->y = (int16_t)((b->y + b->vy) & 8191);
        gfx_px(b->x >> 8, b->y >> 8, gfx_hsv((uint8_t)(HUE[i % 3] + t / 200), 170, 255));
    }
}

/* ---- galaxy: a tilted two-armed spiral turning slowly ----------------------------------------- */
void galaxy_frame(uint32_t t)
{
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int dx = x * 2 - 31, dy = (y * 2 - 31) * 5 / 3, r2 = dx * dx + dy * dy;
            rgb_t c = 0;
            if (r2 < 140) c = gfx_scale(RGB(255, 200, 140), (uint8_t)((140 - r2) * 170 / 140));   /* the bulge */
            else if ((fx_hash((uint32_t)(y * 32 + x)) >> 26) == 0) c = RGB(30, 30, 45);           /* far stars */
            gfx_px(x, y, c);
        }
    uint8_t rot = (uint8_t)(t / 45);
    for (int i = 0; i < 170; i++) {
        uint32_t h = fx_hash((uint32_t)i + 7u);
        int r = 1 + (int)((h >> 8) & 255) * 15 / 256;                  /* 1..15: denser towards the middle */
        uint8_t a = (uint8_t)((i & 1) * 128 + r * 14 + (int)((h >> 24) & 31) - 16 + rot);
        int x = 64 + CS(a) * r * 4 / 128, y = 64 + SN(a) * r * 12 / 5 / 128;    /* 1/4 px, tilted */
        rgb_t c = r < 3 ? RGB(255, 230, 180) : (h & 7) == 0 ? RGB(255, 110, 170) : RGB(140, 175, 255);
        int v = 255 - r * 11 + (int)((h >> 4) & 31);
        gfx_px_add(x >> 2, y >> 2, gfx_scale(c, (uint8_t)(v > 255 ? 255 : v)));
    }
}

/* ---- snowfall: flakes drift down, pile up between the trees, melt, again ---------------------- */
static void flake_reset(flake_t *f, int anywhere)
{
    f->x = (int16_t)gfx_rnd(8192);
    f->y = (int16_t)(anywhere ? (int)gfx_rnd(8192) : -(int)gfx_rnd(2048));
    f->near = (uint8_t)(gfx_rnd(3) == 0);
    f->sp = (uint8_t)(f->near ? 40 + gfx_rnd(30) : 16 + gfx_rnd(16));
}

void snow_init(void)
{
    for (int i = 0; i < NSNOW; i++) flake_reset(&X.snow.f[i], 1);
    for (int x = 0; x < 32; x++) X.snow.pile[x] = 0;
    X.snow.melt = 0;
}

void snow_frame(uint32_t t)
{
    static const uint8_t TX[4] = { 4, 11, 21, 28 }, TH[4] = { 13, 9, 15, 10 };
    for (int y = 0; y < 32; y++) {
        rgb_t c = fx_mix(RGB(2, 4, 16), RGB(22, 28, 62), y * 256 / 31);
        for (int x = 0; x < 32; x++) gfx_px(x, y, c);
    }
    for (int i = 0; i < 4; i++)                                      /* fir trees */
        for (int k = 0; k <= TH[i]; k++) {
            int half = k / 3, y = 31 - TH[i] + k;
            for (int x = TX[i] - half; x <= TX[i] + half; x++) gfx_px(x, y, RGB(4, 22, 12));
        }
    int total = 0;
    for (int x = 0; x < 32; x++) total += X.snow.pile[x];
    if (total > 32 * 9 * 16) X.snow.melt = 1;
    if (X.snow.melt) {
        for (int x = 0; x < 32; x++) if (X.snow.pile[x] && (t / 25) % 3 == 0) X.snow.pile[x]--;
        if (total < 32 * 16) X.snow.melt = 0;
    }
    for (int i = 0; i < NSNOW; i++) {
        flake_t *f = &X.snow.f[i];
        f->y = (int16_t)(f->y + f->sp);
        f->x = (int16_t)((f->x + SN(t / 8 + i * 37) / (f->near ? 2 : 4)) & 8191);
        int x = f->x >> 8, y = f->y >> 8;
        if (y >= 31 - (X.snow.pile[x] >> 4)) {
            if (!X.snow.melt && X.snow.pile[x] < 14 * 16) X.snow.pile[x] = (uint16_t)(X.snow.pile[x] + 5);
            flake_reset(f, 0);
            continue;
        }
        if (y >= 0) gfx_px(x, y, f->near ? RGB(240, 245, 255) : RGB(110, 120, 150));
    }
    for (int x = 0; x < 32; x++) {                                   /* snow slides off steep steps */
        int l = x ? X.snow.pile[x - 1] : X.snow.pile[x], r = x < 31 ? X.snow.pile[x + 1] : X.snow.pile[x];
        if (X.snow.pile[x] > l + 24) { X.snow.pile[x] = (uint16_t)(X.snow.pile[x] - 4); X.snow.pile[x - 1] = (uint16_t)(X.snow.pile[x - 1] + 4); }
        else if (X.snow.pile[x] > r + 24) { X.snow.pile[x] = (uint16_t)(X.snow.pile[x] - 4); X.snow.pile[x + 1] = (uint16_t)(X.snow.pile[x + 1] + 4); }
    }
    for (int x = 0; x < 32; x++) {
        int h = X.snow.pile[x], full = h >> 4;
        for (int k = 0; k < full; k++) gfx_px(x, 31 - k, RGB(215, 225, 250));
        if (h & 15) gfx_px(x, 31 - full, gfx_scale(RGB(215, 225, 250), (uint8_t)((h & 15) * 16)));
    }
}

/* ---- aquarium: fish, swaying weed, bubbles, light from above ---------------------------------- */
static const char FISH[5][8] = { "..###..", "#.####.", "#######", "#.####.", "..###.." };

void aqua_init(void)
{
    for (int i = 0; i < NFISH; i++) {
        fish_t *f = &X.aqua.f[i];
        f->x = (int16_t)(gfx_rnd(32) * 16); f->y = (uint8_t)(5 + i * 5 + gfx_rnd(3)); f->dir = (uint8_t)gfx_rnd(2);
        f->sp = (uint8_t)(3 + gfx_rnd(5)); f->hue = (uint8_t)(i * 64 + gfx_rnd(40)); f->ph = (uint8_t)gfx_rand();
    }
    for (int i = 0; i < NBUB; i++) { X.aqua.b[i].y = (int16_t)(gfx_rnd(30) * 16); X.aqua.b[i].x = (uint8_t)(i < 3 ? 6 : 24); X.aqua.b[i].ph = (uint8_t)gfx_rand(); }
}

void aqua_frame(uint32_t t)
{
    static const uint8_t WX[3] = { 3, 13, 27 }, WH[3] = { 12, 9, 15 };
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            rgb_t c = fx_mix(RGB(20, 110, 165), RGB(0, 20, 65), y * 256 / 28);
            int r = SN(x * 6 + y * 3 - (int)(t / 20));
            if (y < 27 && r > 80) c = fx_mix(c, RGB(130, 220, 255), (r - 80) * (27 - y) / 27);
            if (y >= 29) c = ((x * 5 + y * 3) % 7) ? RGB(150, 125, 70) : RGB(110, 90, 50);   /* sand */
            gfx_px(x, y, c);
        }
    for (int p = 0; p < 3; p++)                                      /* weed */
        for (int k = 0; k < WH[p]; k++) {
            int x = WX[p] + SN(k * 12 + (int)(t / 8) + p * 60) * k / 384;
            gfx_px(x, 28 - k, (k & 1) ? RGB(30, 150, 60) : RGB(20, 110, 45));
        }
    for (int i = 0; i < NBUB; i++) {
        bubble_t *b = &X.aqua.b[i];
        b->y = (int16_t)(b->y - 5);
        if (b->y < 0) { b->y = (int16_t)(28 * 16); b->ph = (uint8_t)gfx_rand(); }
        gfx_px(b->x + SN(b->y * 2 + b->ph) / 100, b->y >> 4, RGB(170, 220, 255));
    }
    for (int i = 0; i < NFISH; i++) {
        fish_t *f = &X.aqua.f[i];
        f->x = (int16_t)(f->x + (f->dir ? -f->sp : f->sp));
        if ((!f->dir && f->x > 40 * 16) || (f->dir && f->x < -9 * 16)) {
            f->dir ^= 1; f->y = (uint8_t)(4 + gfx_rnd(22)); f->sp = (uint8_t)(3 + gfx_rnd(5));
            if (gfx_rnd(3) == 0) f->hue = (uint8_t)gfx_rand();
        }
        int fx = f->x >> 4, fy = f->y + SN(t / 15 + f->ph) * 2 / 128;
        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 7; c++) {
                if (FISH[r][c] != '#') continue;
                int x = f->dir ? fx + 6 - c : fx + c;
                rgb_t col = c < 2 ? gfx_hsv((uint8_t)(f->hue + 20), 255, 150)
                          : (c == 5 && r == 1) ? RGB(10, 10, 20)
                          : gfx_hsv(f->hue, (uint8_t)(c == 3 ? 120 : 230), 255);
                gfx_px(x, fy + r - 2, col);
            }
    }
}

/* ---- fireflies: blinking lights over a dark forest -------------------------------------------- */
void ffly_init(void)
{
    for (int i = 0; i < NFLY; i++) {
        fly_t *f = &X.fly.f[i];
        f->x = (int16_t)gfx_rnd(8192); f->y = (int16_t)((8 + gfx_rnd(20)) * 256); f->vx = 0; f->vy = 0;
        f->ph = (uint8_t)gfx_rand(); f->per = (uint8_t)(3 + gfx_rnd(5));
    }
}

void ffly_frame(uint32_t t)
{
    for (int y = 0; y < 32; y++) {                                   /* dusk: lighter towards the horizon */
        rgb_t c = fx_mix(RGB(2, 4, 20), RGB(26, 34, 70), y * 256 / 22 > 256 ? 256 : y * 256 / 22);
        for (int x = 0; x < 32; x++) gfx_px(x, y, c);
    }
    gfx_circle(26, 5, 2, RGB(210, 210, 180), 1);
    for (int x = 0; x < 32; x++) {                                   /* tree line, then grass */
        int top = 17 + SN(x * 29) / 40 + SN(x * 11 + 40) / 50;
        for (int y = top; y < 32; y++) gfx_px(x, y, RGB(1, 6, 4));
        int g = 2 + (int)(fx_hash((uint32_t)x) % 3u);
        for (int k = 0; k < g; k++) gfx_px(x, 31 - k, RGB(5, 30, 12));
    }
    for (int i = 0; i < NFLY; i++) {
        fly_t *f = &X.fly.f[i];
        f->vx = (int16_t)((f->vx + (int)gfx_rnd(13) - 6) * 15 / 16);
        f->vy = (int16_t)((f->vy + (int)gfx_rnd(13) - 6) * 15 / 16);
        f->x = (int16_t)((f->x + f->vx) & 8191);
        f->y = (int16_t)(f->y + f->vy);
        if (f->y < 6 * 256) { f->y = 6 * 256; f->vy = (int16_t)-f->vy; }
        if (f->y > 28 * 256) { f->y = 28 * 256; f->vy = (int16_t)-f->vy; }
        int b = SN(t * f->per / 64 + f->ph);
        if (b <= 40) continue;
        int g = (b - 40) * 255 / 87, x = f->x >> 8, y = f->y >> 8;
        gfx_px(x, y, RGB(g * 210 / 255, g, g * 50 / 255));
        rgb_t halo = RGB(g / 6, g / 4, 0);
        gfx_px_add(x + 1, y, halo); gfx_px_add(x - 1, y, halo); gfx_px_add(x, y + 1, halo); gfx_px_add(x, y - 1, halo);
    }
}

/* ---- maze: carved (depth first), flooded from the start, the way out traced -------------------- */
#define MZ 15                                                         /* cells a side: 31 x 31 px */

void maze_init(void)
{
    for (int y = 0; y < 31; y++) for (int x = 0; x < 31; x++) X.maze.g[y][x] = 0;
    for (int i = 0; i < MZ * MZ; i++) X.maze.dist[i] = 0xFF;
    X.maze.g[1][1] = 1; X.maze.stk[0] = 0; X.maze.sp = 1; X.maze.phase = 0; X.maze.t0 = 0;
}

static int maze_open(int a, int b)                                    /* the wall between two cells is open */
{
    int ax = a % MZ, ay = a / MZ, bx = b % MZ, by = b / MZ;
    return X.maze.g[ay + by + 1][ax + bx + 1] != 0;
}

static int maze_nbrs(int c, int *n)
{
    int cx = c % MZ, cy = c / MZ, k = 0;
    if (cy > 0) n[k++] = c - MZ;
    if (cx < MZ - 1) n[k++] = c + 1;
    if (cy < MZ - 1) n[k++] = c + MZ;
    if (cx > 0) n[k++] = c - 1;
    return k;
}

void maze_frame(uint32_t t)
{
    int n[4];
    switch (X.maze.phase) {
    case 0:                                                           /* carve */
        for (int s = 0; s < 4 && X.maze.sp; s++) {
            int c = X.maze.stk[X.maze.sp - 1], free[4], k = 0, m = maze_nbrs(c, n);
            for (int i = 0; i < m; i++) if (!X.maze.g[2 * (n[i] / MZ) + 1][2 * (n[i] % MZ) + 1]) free[k++] = n[i];
            if (!k) { X.maze.sp--; continue; }
            int d = free[gfx_rnd((uint32_t)k)];
            X.maze.g[c / MZ + d / MZ + 1][c % MZ + d % MZ + 1] = 1;
            X.maze.g[2 * (d / MZ) + 1][2 * (d % MZ) + 1] = 1;
            X.maze.stk[X.maze.sp++] = (uint8_t)d;
        }
        if (!X.maze.sp) { X.maze.phase = 1; X.maze.t0 = t; }
        break;
    case 1:                                                           /* a breath, then flood from the start */
        if (t - X.maze.t0 < 600) break;
        X.maze.dist[0] = 0; X.maze.q[0] = 0; X.maze.qh = 0; X.maze.qt = 1; X.maze.layer = 0; X.maze.phase = 2;
        break;
    case 2:
        for (int s = 0; s < 2 && X.maze.phase == 2; s++) {
            while (X.maze.qh < X.maze.qt && X.maze.dist[X.maze.q[X.maze.qh]] == X.maze.layer) {
                int c = X.maze.q[X.maze.qh++], m = maze_nbrs(c, n);
                for (int i = 0; i < m; i++)
                    if (X.maze.dist[n[i]] == 0xFF && maze_open(c, n[i])) { X.maze.dist[n[i]] = (uint8_t)(X.maze.layer + 1); X.maze.q[X.maze.qt++] = (uint8_t)n[i]; }
            }
            X.maze.layer++;
            if (X.maze.dist[MZ * MZ - 1] != 0xFF || X.maze.qh >= X.maze.qt) { X.maze.phase = 3; X.maze.cur = MZ * MZ - 1; }
        }
        break;
    case 3:                                                           /* trace back from the exit */
        for (int s = 0; s < 2 && X.maze.phase == 3; s++) {
            int c = X.maze.cur;
            X.maze.g[2 * (c / MZ) + 1][2 * (c % MZ) + 1] = 2;
            if (c == 0 || X.maze.dist[c] == 0xFF) { X.maze.phase = 4; X.maze.t0 = t; break; }
            int m = maze_nbrs(c, n);
            for (int i = 0; i < m; i++)
                if (X.maze.dist[n[i]] == X.maze.dist[c] - 1 && maze_open(c, n[i])) {
                    X.maze.g[c / MZ + n[i] / MZ + 1][c % MZ + n[i] % MZ + 1] = 2;
                    X.maze.cur = (uint8_t)n[i];
                    break;
                }
        }
        break;
    default:
        if (t - X.maze.t0 > 3000) maze_init();
        break;
    }
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            uint8_t g = (x < 31 && y < 31) ? X.maze.g[y][x] : 0;
            rgb_t c;
            if (!g) c = RGB(22, 28, 70);
            else if (g == 2) c = RGB(255, 215, 60);
            else {
                int cx = (x - 1) / 2, cy = (y - 1) / 2;                /* the cell (or one beside the gap) */
                uint8_t d = X.maze.dist[cy * MZ + cx];
                c = d == 0xFF ? RGB(0, 0, 0) : gfx_hsv((uint8_t)(d * 3 + 120), 255, 70);
            }
            gfx_px(x, y, c);
        }
    gfx_px(1, 1, RGB(60, 255, 90)); gfx_px(29, 29, RGB(255, 60, 60));
    if (X.maze.phase == 0 && X.maze.sp) { int c = X.maze.stk[X.maze.sp - 1]; gfx_px(2 * (c % MZ) + 1, 2 * (c / MZ) + 1, RGB(255, 255, 255)); }
}

/* ---- sorting: bubble, cocktail shaker, insertion and selection sort, one after another ---------- */
void sort_init(void)
{
    for (int i = 0; i < 32; i++) X.sort.a[i] = (uint8_t)(i + 1);
    X.sort.algo = 0; X.sort.phase = 0; X.sort.i = 31; X.sort.wait = 0; X.sort.hi1 = X.sort.hi2 = 255;
}

static void sort_start(void)
{
    X.sort.i = X.sort.algo == 2 ? 1 : X.sort.algo == 0 ? 31 : 0; X.sort.j = X.sort.algo == 2 ? 1 : X.sort.algo == 3 ? 1 : 0;
    X.sort.k = 0; X.sort.lo = 0; X.sort.hi = 31; X.sort.dir = 0; X.sort.phase = 1;
}

static void swap8(uint8_t *a, uint8_t *b) { uint8_t v = *a; *a = *b; *b = v; }

static int sort_step(void)                                            /* one comparison; 1 when sorted */
{
    uint8_t *a = X.sort.a;
    switch (X.sort.algo) {
    case 0:                                                           /* bubble */
        X.sort.hi1 = X.sort.j; X.sort.hi2 = (uint8_t)(X.sort.j + 1);
        if (a[X.sort.j] > a[X.sort.j + 1]) swap8(&a[X.sort.j], &a[X.sort.j + 1]);
        if (++X.sort.j >= X.sort.i) { X.sort.j = 0; if (--X.sort.i == 0) return 1; }
        return 0;
    case 1:                                                           /* cocktail shaker */
        X.sort.hi1 = X.sort.j; X.sort.hi2 = (uint8_t)(X.sort.j + 1);
        if (a[X.sort.j] > a[X.sort.j + 1]) swap8(&a[X.sort.j], &a[X.sort.j + 1]);
        if (!X.sort.dir) { if (++X.sort.j >= X.sort.hi) { X.sort.hi--; X.sort.dir = 1; X.sort.j = (uint8_t)(X.sort.hi - 1); } }
        else if (X.sort.j <= X.sort.lo) { X.sort.lo++; X.sort.dir = 0; X.sort.j = X.sort.lo; }
        else X.sort.j--;
        return X.sort.lo >= X.sort.hi;
    case 2:                                                           /* insertion */
        X.sort.hi1 = X.sort.j; X.sort.hi2 = (uint8_t)(X.sort.j - 1);
        if (X.sort.j > 0 && a[X.sort.j - 1] > a[X.sort.j]) { swap8(&a[X.sort.j - 1], &a[X.sort.j]); X.sort.j--; return 0; }
        X.sort.i++; X.sort.j = X.sort.i;
        return X.sort.i > 31;
    default:                                                          /* selection */
        X.sort.hi1 = X.sort.j; X.sort.hi2 = X.sort.k;
        if (a[X.sort.j] < a[X.sort.k]) X.sort.k = X.sort.j;
        if (++X.sort.j > 31) {
            swap8(&a[X.sort.i], &a[X.sort.k]);
            X.sort.i++; X.sort.j = (uint8_t)(X.sort.i + 1); X.sort.k = X.sort.i;
            return X.sort.i >= 31;
        }
        return 0;
    }
}

void sort_frame(uint32_t t)
{
    (void)t;
    switch (X.sort.phase) {
    case 0:                                                           /* shuffle, one swap a frame */
        if (X.sort.i == 0) { sort_start(); break; }
        swap8(&X.sort.a[X.sort.i], &X.sort.a[gfx_rnd(X.sort.i + 1u)]);
        X.sort.hi1 = X.sort.i; X.sort.hi2 = 255;
        X.sort.i--;
        break;
    case 1:
        for (int s = 0; s < 3; s++) if (sort_step()) { X.sort.phase = 2; X.sort.k = 0; X.sort.hi1 = X.sort.hi2 = 255; break; }
        break;
    case 2:                                                           /* sorted: a green sweep */
        if (++X.sort.k > 32) { X.sort.phase = 3; X.sort.wait = 40; }
        break;
    default:
        if (!--X.sort.wait) { X.sort.algo = (uint8_t)((X.sort.algo + 1) & 3); X.sort.phase = 0; X.sort.i = 31; }
        break;
    }
    gfx_fill(0);
    for (int x = 0; x < 32; x++) {
        int h = X.sort.a[x];
        rgb_t c = (x == X.sort.hi1 || x == X.sort.hi2) ? RGB(255, 255, 255)
                : (X.sort.phase >= 2 && x < X.sort.k) ? RGB(40, 230, 90) : gfx_hsv((uint8_t)(h * 7), 220, 210);
        for (int y = 32 - h; y < 32; y++) gfx_px(x, y, c);
    }
}

/* ---- pendulum wave: fifteen pendulums, each a little faster than the one before ---------------- */
void pend_init(void) { gfx_fill(0); }

void pend_frame(uint32_t t)
{
    gfx_fade(95);
    uint32_t p = t % 40000u;                                          /* all in line again every 40 s */
    for (int k = 0; k < 15; k++) {
        uint8_t ph = (uint8_t)(p * (30u + (unsigned)k) * 256u / 40000u);
        int x = 16 * 4 + SN(ph) * 13 * 4 / 128, y = 2 + k * 2;
        rgb_t c = gfx_hsv((uint8_t)(k * 17), 190, 255);
        gfx_px((x >> 2) - 1, y, c); gfx_px(x >> 2, y, c);
        gfx_px_add(16, y, RGB(12, 12, 16));
    }
}

/* ---- brick breaker: the computer plays ------------------------------------------------------- */
void brk_init(void)
{
    for (int r = 0; r < 5; r++) X.brk.row[r] = 0xFF;
    X.brk.bx = 16 * 256; X.brk.by = 24 * 256; X.brk.vx = 110; X.brk.vy = -170; X.brk.px = 16 * 256; X.brk.wait = 0;
}

void brk_frame(uint32_t t)
{
    if (X.brk.wait && !--X.brk.wait) brk_init();
    int nx = X.brk.bx + X.brk.vx, ny = X.brk.by + X.brk.vy;
    if (nx < 0) { nx = -nx; X.brk.vx = (int16_t)-X.brk.vx; }
    if (nx > 31 * 256) { nx = 62 * 256 - nx; X.brk.vx = (int16_t)-X.brk.vx; }
    if (ny < 0) { ny = -ny; X.brk.vy = (int16_t)-X.brk.vy; }
    int px = nx >> 8, py = ny >> 8;
    if (py >= 2 && py < 17 && (py - 2) % 3 < 2 && px % 4 < 3) {        /* a brick? */
        int r = (py - 2) / 3, c = px / 4;
        if (X.brk.row[r] & (1u << c)) { X.brk.row[r] &= (uint8_t)~(1u << c); X.brk.vy = (int16_t)-X.brk.vy; ny = X.brk.by; }
    }
    int pc = X.brk.px >> 8;                                           /* paddle: 6 px around pc, row 30 */
    if (X.brk.vy > 0 && py >= 29 && py <= 30 && px >= pc - 3 && px <= pc + 2) {
        X.brk.vy = (int16_t)-X.brk.vy;
        int vx = X.brk.vx + (px - pc) * 40;
        X.brk.vx = (int16_t)(vx > 200 ? 200 : vx < -200 ? -200 : vx);
        if (X.brk.vx > -40 && X.brk.vx < 40) X.brk.vx = (int16_t)(X.brk.vx < 0 ? -60 : 60);
        ny = 29 * 256;
    }
    if (py > 31) { nx = X.brk.px; ny = 28 * 256; X.brk.vy = -170; }      /* missed: serve again */
    X.brk.bx = (int16_t)nx; X.brk.by = (int16_t)ny;
    int aim = nx + SN(t / 70) * 2 * 256 / 128;                         /* not dead centre: angles */
    int d = aim - X.brk.px;
    X.brk.px = (int16_t)(X.brk.px + (d > 230 ? 230 : d < -230 ? -230 : d));
    int left = 0;
    for (int r = 0; r < 5; r++) left |= X.brk.row[r];
    if (!left && !X.brk.wait) X.brk.wait = 50;
    gfx_fill(0);
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 8; c++)
            if (X.brk.row[r] & (1u << c)) gfx_rect(c * 4, 2 + r * 3, 3, 2, gfx_hsv((uint8_t)(r * 40), 230, 230), 1);
    gfx_rect((X.brk.px >> 8) - 3, 30, 6, 1, RGB(200, 210, 255), 1);
    gfx_px(X.brk.bx >> 8, X.brk.by >> 8, RGB(255, 255, 255));
}

/* ---- meadow: hills, clouds and grass in the wind --------------------------------------------- */
void meadow_frame(uint32_t t)
{
    static const uint8_t FX[5] = { 4, 10, 17, 23, 29 }, FH[5] = { 6, 8, 5, 7, 6 };
    static const rgb_t FC[5] = { RGB(255, 60, 60), RGB(255, 220, 40), RGB(190, 90, 255), RGB(255, 255, 255), RGB(255, 120, 190) };
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            rgb_t c = fx_mix(RGB(40, 110, 225), RGB(170, 215, 255), y * 256 / 20 > 256 ? 256 : y * 256 / 20);
            if (y < 11) {                                             /* clouds */
                int v = SN(x * 9 + (int)(t / 50)) + SN(x * 5 - (int)(t / 70) + y * 25) - (y - 5) * (y - 5) * 6;
                if (v > 60) c = fx_mix(c, RGB(250, 250, 255), (v - 60) * 3 > 256 ? 256 : (v - 60) * 3);
            }
            int dx = x - 25, dy = y - 5, d2 = dx * dx + dy * dy;           /* sun */
            if (d2 <= 6) c = RGB(255, 245, 190); else if (d2 < 30) c = fx_mix(c, RGB(255, 240, 180), (30 - d2) * 6);
            if (y >= 19 + SN(x * 6 + 30) * 3 / 128) c = RGB(70, 140, 115);       /* far hill */
            if (y >= 23 + SN(x * 4 + 100) * 2 / 128) c = fx_mix(RGB(70, 170, 60), RGB(30, 100, 30), (y - 21) * 25);   /* near hill */
            gfx_px(x, y, c);
        }
    int gust = 60 + SN(t / 37) / 2;                                    /* the wind comes and goes */
    for (int x = 0; x < 32; x++) {
        int h = 3 + (int)(fx_hash((uint32_t)x * 3u) % 4u);
        int off = SN((int)(t / 12) - x * 6) * gust * h / (128 * 128 * 2);
        gfx_line(x, 31, x + off, 31 - h, (x & 1) ? RGB(40, 160, 50) : RGB(90, 200, 60));
    }
    for (int i = 0; i < 5; i++) {
        int off = SN((int)(t / 12) - FX[i] * 6) * gust * FH[i] / (128 * 128 * 2);
        gfx_line(FX[i], 31, FX[i] + off, 31 - FH[i], RGB(30, 120, 40));
        int hx = FX[i] + off, hy = 31 - FH[i];
        gfx_px(hx, hy - 1, FC[i]); gfx_px(hx - 1, hy, FC[i]); gfx_px(hx + 1, hy, FC[i]); gfx_px(hx, hy + 1, FC[i]);
        gfx_px(hx, hy, RGB(255, 200, 40));
    }
}
