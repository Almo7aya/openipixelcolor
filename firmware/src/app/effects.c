/* Procedural effects for the 32x32 panel. Integer maths only; one effect runs at a time, so all state
 * lives in a shared union (RAM is tight on this chip). */
#include "effects.h"
#include "fx_more.h"
#include "fx_demo.h"
#include "fx_sim.h"
#include "fx_finals.h"
#include "gfx.h"
#include "tables.h"

#define CLAMP8(v) ((v) < 0 ? 0 : (v) > 255 ? 255 : (v))

/* ------------------------------------------------------------------------------------------------ */
#define NSTAR 64
#define NPART 160
#define NBALL 7

typedef struct { int16_t x, y, z; } star_t;
typedef struct { int16_t x, y, vx, vy; uint8_t life, hue, rocket; } part_t;
typedef struct { int16_t y; uint8_t speed, active; } drop_t;
typedef struct { int16_t x, y, vx, vy; uint8_t hue; } ball_t;

static union {
    uint8_t heat[34][32];                                           /* fire */
    struct { drop_t d[32]; } mx;                                    /* matrix */
    struct { star_t s[NSTAR]; } sf;                                 /* starfield */
    struct { uint8_t c[32][32], n[32][32]; uint8_t hue; uint16_t gen, stale, pop; uint32_t last; } life;
    struct { part_t p[NPART]; uint32_t next; } fw;                  /* fireworks */
    struct { int16_t a[32][32], b[32][32]; uint8_t flip; uint32_t next; } rip;
    struct { uint8_t bx[256], by[256]; uint8_t occ[16][16]; uint16_t len, head;
             uint8_t ax, ay; int8_t dx, dy; uint32_t last, dead; } sn;          /* snake */
    struct { int32_t bx, by, vx, vy, p1, p2; uint8_t s1, s2; } pong;
    struct { ball_t b[NBALL]; } balls;
    struct { uint8_t lvl[11], target[11]; uint32_t last; } eq;
    uint32_t scratch[FX_SCRATCH_BYTES / 4];                         /* fx_more.c */
} M;

void *fx_scratch(void) { return &M; }

static void nop_init(void) { }

/* ---- plasma ---------------------------------------------------------------------------------- */
static void plasma_frame(uint32_t t)
{
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            unsigned v = sin8[(uint8_t)(x * 10 + t / 12)] + sin8[(uint8_t)(y * 12 + t / 9)]
                       + sin8[(uint8_t)((x + y) * 7 + t / 15)] + sin8[(uint8_t)(dist32[y][x] * 2 - t / 7)];
            gfx_px(x, y, gfx_hsv((uint8_t)(v / 4 + t / 40), 255, 255));
        }
}

/* ---- fire ------------------------------------------------------------------------------------ */
static rgb_t fire_pal(unsigned h)
{
    if (h < 85)  return RGB(h * 3, 0, 0);
    if (h < 170) return RGB(255, (h - 85) * 3, 0);
    return RGB(255, 255, (h - 170) * 3);
}

static void fire_init(void)
{
    for (int y = 0; y < 34; y++) for (int x = 0; x < 32; x++) M.heat[y][x] = 0;
}

/* "Doom fire": intensity 0..36 spreads upwards with random sideways drift and decay -> flame tongues */
static void fire_frame(uint32_t t)
{
    (void)t;
    for (int x = 0; x < 32; x++) M.heat[33][x] = (uint8_t)(gfx_rnd(100) < 92 ? 36 : 20);   /* flickering source */
    for (int y = 1; y < 34; y++)
        for (int x = 0; x < 32; x++) {
            unsigned r = gfx_rand();
            int src = M.heat[y][x];
            int dx = (int)(r & 3u) - 1;
            int dec = (int)((r >> 2) & 3u);                                         /* avg 1.5 -> ~24 rows tall */
            int v = src - dec;
            M.heat[y - 1][(x + dx) & 31] = (uint8_t)(v < 0 ? 0 : v);
        }
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) gfx_px(x, y, fire_pal(M.heat[y + 1][x] * 7u));
}

/* ---- matrix rain ----------------------------------------------------------------------------- */
static void matrix_init(void)
{
    gfx_fill(0);
    for (int i = 0; i < 32; i++) M.mx.d[i].active = 0;
}

static void matrix_frame(uint32_t t)
{
    (void)t;
    gfx_fade(222);
    for (int x = 0; x < 32; x++) {
        drop_t *d = &M.mx.d[x];
        if (!d->active) {
            if (gfx_rnd(100) < 5) { d->active = 1; d->y = (int16_t)(-(int)gfx_rnd(8) * 16); d->speed = (uint8_t)(6 + gfx_rnd(14)); }
            continue;
        }
        d->y = (int16_t)(d->y + d->speed);
        int py = d->y >> 4;
        gfx_px(x, py - 1, RGB(0, 200, 30));
        gfx_px(x, py, RGB(190, 255, 190));
        if (py > 44) d->active = 0;
    }
}

/* ---- starfield ------------------------------------------------------------------------------- */
static void star_reset(int i, int far)
{
    M.sf.s[i].x = (int16_t)((int)gfx_rnd(2048) - 1024);
    M.sf.s[i].y = (int16_t)((int)gfx_rnd(2048) - 1024);
    M.sf.s[i].z = (int16_t)(far ? 1024 : 16 + (int)gfx_rnd(1008));
}

static void stars_init(void) { gfx_fill(0); for (int i = 0; i < NSTAR; i++) star_reset(i, 0); }

static void stars_frame(uint32_t t)
{
    (void)t;
    gfx_fade(110);
    for (int i = 0; i < NSTAR; i++) {
        star_t *s = &M.sf.s[i];
        s->z = (int16_t)(s->z - 10);
        if (s->z <= 16) { star_reset(i, 1); continue; }
        int sx = 16 + s->x * 4 / s->z, sy = 16 + s->y * 4 / s->z;
        if (sx < 0 || sx > 31 || sy < 0 || sy > 31) { star_reset(i, 1); continue; }
        int b = 255 - s->z / 4;
        gfx_px(sx, sy, RGB(b, b, CLAMP8(b + 40)));
    }
}

/* ---- game of life ---------------------------------------------------------------------------- */
static void life_seed(void)
{
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) M.life.c[y][x] = gfx_rnd(100) < 32 ? 1 : 0;
    M.life.hue = (uint8_t)gfx_rand();
    M.life.gen = M.life.stale = M.life.pop = 0;
}

static void life_init(void) { gfx_fill(0); life_seed(); M.life.last = 0; }

static void life_step(void)
{
    unsigned pop = 0;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int n = 0;
            for (int j = -1; j <= 1; j++)
                for (int i = -1; i <= 1; i++)
                    if ((i || j) && M.life.c[(y + j) & 31][(x + i) & 31]) n++;
            uint8_t a = M.life.c[y][x];
            uint8_t nv = (n == 3 || (a && n == 2)) ? (uint8_t)(a ? (a < 250 ? a + 1 : a) : 1) : 0;
            M.life.n[y][x] = nv;
            pop += nv != 0;
        }
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) M.life.c[y][x] = M.life.n[y][x];
    M.life.stale = (pop == M.life.pop) ? (uint16_t)(M.life.stale + 1) : 0;
    M.life.pop = (uint16_t)pop;
    if (++M.life.gen > 700 || M.life.stale > 40 || pop < 10) life_seed();
}

static void life_frame(uint32_t t)
{
    if (t - M.life.last >= 110) { M.life.last = t; life_step(); }
    gfx_fade(150);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            if (M.life.c[y][x]) gfx_px(x, y, gfx_hsv((uint8_t)(M.life.hue + M.life.c[y][x] * 5), 210, 255));
}

/* ---- fireworks ------------------------------------------------------------------------------- */
static part_t *part_alloc(void)
{
    for (int i = 0; i < NPART; i++) if (!M.fw.p[i].life) return &M.fw.p[i];
    return 0;
}

static void fw_init(void) { gfx_fill(0); for (int i = 0; i < NPART; i++) M.fw.p[i].life = 0; M.fw.next = 0; }

static void fw_frame(uint32_t t)
{
    gfx_fade(200);
    if (t >= M.fw.next) {
        part_t *r = part_alloc();
        if (r) {
            r->x = (int16_t)((6 + gfx_rnd(20)) << 8); r->y = 31 << 8;
            r->vx = (int16_t)((int)gfx_rnd(80) - 40); r->vy = (int16_t)(-(360 + (int)gfx_rnd(150)));
            r->life = 255; r->rocket = 1; r->hue = (uint8_t)gfx_rand();
        }
        M.fw.next = t + 350 + gfx_rnd(900);
    }
    for (int i = 0; i < NPART; i++) {
        part_t *p = &M.fw.p[i];
        if (!p->life) continue;
        p->x = (int16_t)(p->x + p->vx); p->y = (int16_t)(p->y + p->vy);
        p->vy = (int16_t)(p->vy + (p->rocket ? 10 : 4));
        int px = p->x >> 8, py = p->y >> 8;
        if (p->rocket) {
            gfx_px(px, py, RGB(255, 230, 160));
            gfx_px_add(px, py + 1, RGB(90, 50, 10));
            if (p->vy >= -30) {                                  /* apex: explode */
                int n = 26 + (int)gfx_rnd(10);
                for (int k = 0; k < n; k++) {
                    part_t *q = part_alloc();
                    if (!q) break;
                    uint8_t a = (uint8_t)(k * 256 / n + gfx_rnd(8));
                    int sp = 110 + (int)gfx_rnd(150);
                    q->x = p->x; q->y = p->y;
                    q->vx = (int16_t)(((int)cos8(a) - 128) * sp / 128);
                    q->vy = (int16_t)(((int)sin8[a] - 128) * sp / 128);
                    q->life = (uint8_t)(150 + gfx_rnd(100)); q->hue = (uint8_t)(p->hue + gfx_rnd(24)); q->rocket = 0;
                }
                p->life = 0;
            }
            continue;
        }
        p->vx = (int16_t)(p->vx * 245 / 256);
        p->vy = (int16_t)(p->vy * 245 / 256);
        p->life = p->life > 5 ? (uint8_t)(p->life - 5) : 0;
        if (px < 0 || px > 31 || py > 31) { p->life = 0; continue; }
        gfx_px(px, py, gfx_hsv(p->hue, p->life > 200 ? 90 : 200, (uint8_t)(p->life > 60 ? 255 : p->life * 4)));
    }
}

/* ---- water ripples --------------------------------------------------------------------------- */
static void rip_init(void)
{
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) M.rip.a[y][x] = M.rip.b[y][x] = 0;
    M.rip.flip = 0; M.rip.next = 0;
}

static void rip_frame(uint32_t t)
{
    int16_t (*cur)[32] = M.rip.flip ? M.rip.b : M.rip.a;
    int16_t (*old)[32] = M.rip.flip ? M.rip.a : M.rip.b;
    if (t >= M.rip.next) {
        int x = 2 + (int)gfx_rnd(28), y = 2 + (int)gfx_rnd(28);
        cur[y][x] = 900; cur[y][x + 1] = cur[y + 1][x] = 600;
        M.rip.next = t + 250 + gfx_rnd(900);
    }
    for (int y = 1; y < 31; y++)
        for (int x = 1; x < 31; x++) {
            int v = ((cur[y - 1][x] + cur[y + 1][x] + cur[y][x - 1] + cur[y][x + 1]) >> 1) - old[y][x];
            v -= v >> 5;
            old[y][x] = (int16_t)v;
        }
    M.rip.flip ^= 1;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int v = old[y][x];
            int b = CLAMP8(50 + v);
            int w = b > 170 ? (b - 170) * 3 : 0;
            gfx_px(x, y, RGB(CLAMP8(w), CLAMP8(b / 3 + w), CLAMP8(b)));
        }
}

/* ---- sparkles -------------------------------------------------------------------------------- */
static void sparkle_init(void) { gfx_fill(0); }

static void sparkle_frame(uint32_t t)
{
    (void)t;
    gfx_fade(228);
    for (int i = 0; i < 3; i++)
        gfx_px(gfx_rnd(32), gfx_rnd(32), gfx_hsv((uint8_t)gfx_rand(), (uint8_t)(120 + gfx_rnd(136)), 255));
}

/* ---- rainbow swirl --------------------------------------------------------------------------- */
static void swirl_frame(uint32_t t)
{
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            uint8_t h = (uint8_t)(ang32[y][x] + dist32[y][x] * 3 + t / 8);
            unsigned v = sin8[(uint8_t)(dist32[y][x] * 3 - t / 5)];
            gfx_px(x, y, gfx_hsv(h, 240, (uint8_t)(70 + v * 185 / 255)));
        }
}

/* ---- tunnel ---------------------------------------------------------------------------------- */
static void tunnel_frame(uint32_t t)
{
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            unsigned d = dist32[y][x];
            unsigned depth = 4096u / (d + 12u);
            unsigned u = (unsigned)(ang32[y][x] + t / 24);
            unsigned v = depth + t / 5;
            unsigned check = ((u >> 5) ^ (v >> 4)) & 1u;
            unsigned shade = d * 2u + 30u; if (shade > 255) shade = 255;
            gfx_px(x, y, gfx_hsv((uint8_t)(v / 2 + t / 60), 220, (uint8_t)(check ? shade : shade / 4)));
        }
}

/* ---- snake (16x16 cells of 2x2 px, plays itself) --------------------------------------------- */
static void sn_apple(void)
{
    for (int tries = 0; tries < 400; tries++) {
        uint8_t x = (uint8_t)gfx_rnd(16), y = (uint8_t)gfx_rnd(16);
        if (!M.sn.occ[y][x]) { M.sn.ax = x; M.sn.ay = y; return; }
    }
}

static void sn_init(void)
{
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) M.sn.occ[y][x] = 0;
    M.sn.len = 3; M.sn.head = 2;
    for (int i = 0; i < 3; i++) { M.sn.bx[i] = (uint8_t)(6 + i); M.sn.by[i] = 8; M.sn.occ[8][6 + i] = 1; }
    M.sn.dx = 1; M.sn.dy = 0; M.sn.last = 0; M.sn.dead = 0;
    sn_apple();
}

static int sn_free(int x, int y) { return x >= 0 && x < 16 && y >= 0 && y < 16 && !M.sn.occ[y][x]; }

static void sn_step(uint32_t t)
{
    static const int8_t DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
    int hx = M.sn.bx[M.sn.head], hy = M.sn.by[M.sn.head];
    int best = -1, best_score = -100000;
    for (int k = 0; k < 4; k++) {
        if (DX[k] == -M.sn.dx && DY[k] == -M.sn.dy) continue;
        int nx = hx + DX[k], ny = hy + DY[k];
        if (!sn_free(nx, ny)) continue;
        int room = 0;
        for (int j = 0; j < 4; j++) room += sn_free(nx + DX[j], ny + DY[j]);
        int dist = (nx > M.sn.ax ? nx - M.sn.ax : M.sn.ax - nx) + (ny > M.sn.ay ? ny - M.sn.ay : M.sn.ay - ny);
        int score = -dist * 4 + room * 3 + (int)gfx_rnd(3) - (room == 0 ? 1000 : 0);
        if (score > best_score) { best_score = score; best = k; }
    }
    if (best < 0) { M.sn.dead = t; return; }
    M.sn.dx = DX[best]; M.sn.dy = DY[best];
    int nx = hx + M.sn.dx, ny = hy + M.sn.dy;
    int eat = (nx == M.sn.ax && ny == M.sn.ay);
    if (!eat) {                                         /* drop the tail */
        uint16_t tail = (uint16_t)((M.sn.head + 256 - (M.sn.len - 1)) & 255);
        M.sn.occ[M.sn.by[tail]][M.sn.bx[tail]] = 0;
    } else if (M.sn.len < 250) {
        M.sn.len++;
    }
    M.sn.head = (uint16_t)((M.sn.head + 1) & 255);
    M.sn.bx[M.sn.head] = (uint8_t)nx; M.sn.by[M.sn.head] = (uint8_t)ny;
    M.sn.occ[ny][nx] = 1;
    if (eat) sn_apple();
}

static void sn_frame(uint32_t t)
{
    if (M.sn.dead) {
        if (t - M.sn.dead > 1200) sn_init();
        gfx_fill(((t / 150) & 1) ? RGB(60, 0, 0) : 0);
        return;
    }
    if (t - M.sn.last >= 70) { M.sn.last = t; sn_step(t); }
    gfx_fill(RGB(0, 0, 6));
    for (uint16_t i = 0; i < M.sn.len; i++) {
        uint16_t idx = (uint16_t)((M.sn.head + 256 - i) & 255);
        rgb_t c = i == 0 ? RGB(200, 255, 120) : gfx_hsv((uint8_t)(90 + i * 2), 230, (uint8_t)(255 - (i * 120) / (M.sn.len + 1)));
        gfx_rect(M.sn.bx[idx] * 2, M.sn.by[idx] * 2, 2, 2, c, 1);
    }
    gfx_rect(M.sn.ax * 2, M.sn.ay * 2, 2, 2, ((t / 250) & 1) ? RGB(255, 30, 30) : RGB(160, 0, 0), 1);
}

/* ---- pong (AI vs AI) ------------------------------------------------------------------------- */
static void pong_serve(int dir)
{
    M.pong.bx = 16 << 8; M.pong.by = (int32_t)(8 + gfx_rnd(16)) << 8;
    M.pong.vx = dir * 120; M.pong.vy = (int32_t)gfx_rnd(160) - 80;
}

static void pong_init(void) { M.pong.p1 = M.pong.p2 = 16 << 8; M.pong.s1 = M.pong.s2 = 0; pong_serve(1); }

static void pong_paddle(int32_t *p, int towards)
{
    int32_t target = towards ? M.pong.by : (16 << 8);
    int32_t d = target - *p, mx = 85;
    *p += d > mx ? mx : d < -mx ? -mx : d;
    if (*p < (4 << 8)) *p = 4 << 8;
    if (*p > (28 << 8)) *p = 28 << 8;
}

static void pong_frame(uint32_t t)
{
    (void)t;
    pong_paddle(&M.pong.p1, M.pong.vx < 0);
    pong_paddle(&M.pong.p2, M.pong.vx > 0);
    M.pong.bx += M.pong.vx; M.pong.by += M.pong.vy;
    if (M.pong.by < 0)        { M.pong.by = -M.pong.by; M.pong.vy = -M.pong.vy; }
    if (M.pong.by > (31 << 8)) { M.pong.by = (62 << 8) - M.pong.by; M.pong.vy = -M.pong.vy; }
    int by = M.pong.by >> 8;
    if (M.pong.vx < 0 && M.pong.bx <= (2 << 8)) {
        int off = by - (M.pong.p1 >> 8);
        if (off >= -4 && off <= 4) { M.pong.vx = -M.pong.vx + 4; M.pong.vy += off * 22; }
        else { if (M.pong.s2 < 99) M.pong.s2++; pong_serve(1); }
    } else if (M.pong.vx > 0 && M.pong.bx >= (29 << 8)) {
        int off = by - (M.pong.p2 >> 8);
        if (off >= -4 && off <= 4) { M.pong.vx = -M.pong.vx - 4; M.pong.vy += off * 22; }
        else { if (M.pong.s1 < 99) M.pong.s1++; pong_serve(-1); }
    }
    if (M.pong.vx > 260) M.pong.vx = 260;
    if (M.pong.vx < -260) M.pong.vx = -260;
    gfx_fill(0);
    for (int y = 0; y < 32; y += 4) gfx_px(16, y, RGB(40, 40, 40)), gfx_px(16, y + 1, RGB(40, 40, 40));
    gfx_7seg(9, 1, 3, 5, 1, M.pong.s1 % 10, RGB(60, 60, 120));
    gfx_7seg(20, 1, 3, 5, 1, M.pong.s2 % 10, RGB(120, 60, 60));
    gfx_rect(0, (M.pong.p1 >> 8) - 3, 2, 7, RGB(80, 140, 255), 1);
    gfx_rect(30, (M.pong.p2 >> 8) - 3, 2, 7, RGB(255, 110, 80), 1);
    gfx_px(M.pong.bx >> 8, by, RGB(255, 255, 255));
}

/* ---- bouncing balls -------------------------------------------------------------------------- */
static void balls_init(void)
{
    gfx_fill(0);
    for (int i = 0; i < NBALL; i++) {
        ball_t *b = &M.balls.b[i];
        b->x = (int16_t)((4 + gfx_rnd(24)) << 8); b->y = (int16_t)((4 + gfx_rnd(24)) << 8);
        b->vx = (int16_t)((int)gfx_rnd(160) - 80); b->vy = (int16_t)((int)gfx_rnd(160) - 80);
        b->hue = (uint8_t)(i * 256 / NBALL);
    }
}

static void balls_frame(uint32_t t)
{
    (void)t;
    gfx_fade(175);
    for (int i = 0; i < NBALL; i++) {
        ball_t *b = &M.balls.b[i];
        b->x = (int16_t)(b->x + b->vx); b->y = (int16_t)(b->y + b->vy);
        if (b->x < (1 << 8) || b->x > (30 << 8)) { b->vx = (int16_t)-b->vx; b->x = (int16_t)(b->x + b->vx); }
        if (b->y < (1 << 8) || b->y > (30 << 8)) { b->vy = (int16_t)-b->vy; b->y = (int16_t)(b->y + b->vy); }
        int x = b->x >> 8, y = b->y >> 8;
        rgb_t c = gfx_hsv(b->hue++, 230, 255);
        gfx_px(x, y, c); gfx_px(x + 1, y, c); gfx_px(x - 1, y, c); gfx_px(x, y + 1, c); gfx_px(x, y - 1, c);
    }
}

/* ---- equalizer ------------------------------------------------------------------------------- */
void eq_draw(const uint8_t lvl[11], unsigned style, uint32_t t)
{
    gfx_fill(0);
    for (int i = 0; i < 11; i++) {
        int h = (lvl[i] > 15 ? 15 : lvl[i]) * 2;          /* 0..30 px */
        int x = i * 3;
        for (int j = 0; j < h; j++) {
            rgb_t c;
            switch (style % 3) {
            case 0:  c = gfx_hsv((uint8_t)(96 - j * 3), 255, 255); break;            /* green -> red */
            case 1:  c = gfx_hsv((uint8_t)(i * 23 + t / 20), 230, 255); break;       /* rainbow bars */
            default: c = gfx_hsv((uint8_t)(160 + j * 3), 200, (uint8_t)(120 + j * 4)); break;
            }
            if (style % 3 == 2) {                                                     /* mirrored */
                gfx_px(x, 16 - h / 2 + j, c); gfx_px(x + 1, 16 - h / 2 + j, c);
            } else {
                gfx_px(x, 31 - j, c); gfx_px(x + 1, 31 - j, c);
            }
        }
    }
}

static void eq_init(void) { for (int i = 0; i < 11; i++) M.eq.lvl[i] = M.eq.target[i] = 0; M.eq.last = 0; }

static void eq_frame(uint32_t t)
{
    if (t - M.eq.last > 120) {
        M.eq.last = t;
        for (int i = 0; i < 11; i++) {
            int bass = i < 3 ? 5 : 0;
            M.eq.target[i] = (uint8_t)(gfx_rnd(11) + bass > 15 ? 15 : gfx_rnd(11) + bass);
        }
    }
    for (int i = 0; i < 11; i++) {
        if (M.eq.lvl[i] < M.eq.target[i]) M.eq.lvl[i]++;
        else if (M.eq.lvl[i] > M.eq.target[i]) M.eq.lvl[i]--;
    }
    eq_draw(M.eq.lvl, (t / 8000) % 3, t);
}

/* ------------------------------------------------------------------------------------------------ */
const effect_t effects[] = {
    { "plasma",    nop_init,     plasma_frame  },
    { "fire",      fire_init,    fire_frame    },
    { "matrix",    matrix_init,  matrix_frame  },
    { "starfield", stars_init,   stars_frame   },
    { "life",      life_init,    life_frame    },
    { "fireworks", fw_init,      fw_frame      },
    { "ripples",   rip_init,     rip_frame     },
    { "sparkles",  sparkle_init, sparkle_frame },
    { "swirl",     nop_init,     swirl_frame   },
    { "tunnel",    nop_init,     tunnel_frame  },
    { "snake",     sn_init,      sn_frame      },
    { "pong",      pong_init,    pong_frame    },
    { "balls",     balls_init,   balls_frame   },
    { "equalizer", eq_init,      eq_frame      },
    /* fx_more.c */
    { "aurora",    fx_nop_init,  aurora_frame  },
    { "lava",      fx_nop_init,  lava_frame    },
    { "rain",      rain_init,    rain_frame    },
    { "dna",       fx_nop_init,  dna_frame     },
    { "warp",      warp_init,    warp_frame    },
    { "radar",     radar_init,   radar_frame   },
    /* fx_demo.c (20..33) */
    { "rotozoom",  fx_nop_init,  rotozoom_frame },
    { "twister",   fx_nop_init,  twister_frame  },
    { "copper",    fx_nop_init,  copper_frame   },
    { "shadebobs", bobs_init,    bobs_frame     },
    { "kaleido",   fx_nop_init,  kaleido_frame  },
    { "julia",     fx_nop_init,  julia_frame    },
    { "moire",     fx_nop_init,  moire_frame    },
    { "sunburst",  fx_nop_init,  sunburst_frame },
    { "spiro",     spiro_init,   spiro_frame    },
    { "cube",      cube_init,    cube_frame     },
    { "synthwave", fx_nop_init,  synth_frame    },
    { "terrain",   terrain_init, terrain_frame  },
    { "globe",     globe_init,   globe_frame    },
    { "tartan",    fx_nop_init,  tartan_frame   },
    /* fx_sim.c (34..46) */
    { "ocean",     fx_nop_init,  ocean_frame    },
    { "sand",      sand_init,    sand_frame     },
    { "ants",      ants_init,    ants_frame     },
    { "flock",     flock_init,   flock_frame    },
    { "galaxy",    fx_nop_init,  galaxy_frame   },
    { "snow",      snow_init,    snow_frame     },
    { "aquarium",  aqua_init,    aqua_frame     },
    { "fireflies", ffly_init,    ffly_frame     },
    { "maze",      maze_init,    maze_frame     },
    { "sorting",   sort_init,    sort_frame     },
    { "pendulums", pend_init,    pend_frame     },
    { "bricks",    brk_init,     brk_frame      },
    { "meadow",    fx_nop_init,  meadow_frame   },
    /* fx_finals.c (47..54): fan-made THE FINALS logo animations */
    { "finals",         fin_intro_init, fin_intro_frame   },
    { "finals-live",    fin_live_init,  fin_live_frame    },
    { "finals-cash",    fin_cash_init,  fin_cash_frame    },
    { "finals-spin",    fin_spin_init,  fin_spin_frame    },
    { "finals-chrome",  fin_em_init,    fin_chrome_frame  },
    { "finals-crt",     fin_em_init,    fin_crt_frame     },
    { "finals-neon",    fin_em_init,    fin_neon_frame    },
    { "finals-shatter", fin_em_init,    fin_shatter_frame },
};
const unsigned effect_count = sizeof effects / sizeof effects[0];
