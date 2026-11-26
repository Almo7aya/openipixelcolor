/* THE FINALS: eight fan-made animations of the game's logo (a pixel version of the wordmark and of the
 * emblem, the rounded three-legged "A" from tools/gen_emblem.py, in the brand colours red #D21F3C, white and
 * black): intro, live broadcast, cash-out, spin, chrome, CRT, neon and shatter.
 *
 * THE FINALS, its name and its emblem belong to Embark Studios. These animations are made by fans, not by
 * or with Embark Studios, and the project's licence doesn't cover the name or the emblem.
 *
 * Same rules as effects.c: integer maths only, all state in the shared scratch memory (one effect runs at a
 * time), frame(t) gets the time in ms and is called ~40x per second. */
#include "fx_finals.h"
#include "effects.h"
#include "emblem.h"
#include "gfx.h"
#include "tables.h"

#define RED    RGB(210, 31, 60)
#define WHITE  RGB(240, 240, 240)

typedef struct { int16_t x, y, vx, vy; uint8_t life, r, g, b; } spark_t;     /* 8.8 fixed point */
typedef struct { int16_t x, y; uint8_t speed, phase; } coin_t;

#define NSPARK 280
#define NCOIN  24

typedef union {
    struct { uint32_t t0, last; uint8_t started, landed, exploded; spark_t s[NSPARK]; } in;
    struct { uint32_t t0; uint8_t started; } live;
    struct { uint32_t t0; uint8_t started; coin_t c[NCOIN]; } cash;
    struct { uint32_t t0; uint8_t started; uint8_t img[32][32][3]; } spin;
    struct { uint32_t t0; uint8_t started; } em;                             /* emblem effects */
} fx_t;

_Static_assert(sizeof(fx_t) <= FX_SCRATCH_BYTES, "fx_finals state does not fit the shared effect memory");
#define X (*(fx_t *)fx_scratch())

static int iabs(int v) { return v < 0 ? -v : v; }

/* ================================================================================================
   THE FINALS logo: "THE" (3x5 font, red) over a bold 7-row "FINALS" (white), red bar under it
   ================================================================================================ */
typedef struct { uint8_t w; const char *rows[7]; } glyph_t;
static const glyph_t FIN[6] = {
    { 4, { "####", "##..", "##..", "###.", "##..", "##..", "##.." } },
    { 2, { "##", "##", "##", "##", "##", "##", "##" } },
    { 6, { "##..##", "###.##", "###.##", "##.###", "##.###", "##..##", "##..##" } },
    { 5, { ".###.", "##.##", "##.##", "#####", "##.##", "##.##", "##.##" } },
    { 4, { "##..", "##..", "##..", "##..", "##..", "##..", "####" } },
    { 4, { ".###", "##..", "##..", ".##.", "..##", "..##", "###." } },
};
#define LX      1          /* FINALS is 30 px wide: x 1..30 */
#define LY_THE  7
#define LY_FIN  14
#define LY_BAR  23

static int fin_x(int i) { int x = LX; for (int k = 0; k < i; k++) x += FIN[k].w + 1; return x; }

static void draw_letter(int i, int x, int y, rgb_t c)
{
    for (int r = 0; r < 7; r++)
        for (int k = 0; k < FIN[i].w; k++)
            if (FIN[i].rows[r][k] == '#') gfx_px(x + k, y + r, c);
}

static void draw_finals(int dx, int y, rgb_t c) { for (int i = 0; i < 6; i++) draw_letter(i, fin_x(i) + dx, y, c); }
static void draw_the(int dx, int y, rgb_t c)    { gfx_tiny_text(11 + dx, y, "THE", c); }

static void draw_logo(int dx, int dy, rgb_t white, rgb_t red, int bar_w)
{
    draw_the(dx, LY_THE + dy, red);
    draw_finals(dx, LY_FIN + dy, white);
    if (bar_w > 0) gfx_rect(16 - bar_w / 2 + dx, LY_BAR + dy, bar_w, 2, red, 1);
}

/* stage spotlights: two cones from below the panel, sweeping */
static void spotlights(uint32_t t, int level)
{
    for (int k = 0; k < 2; k++) {
        int ox = k ? 26 : 5, oy = 42;
        int sw = ((int)sin8[(uint8_t)(t / 13 + k * 100)] - 128) * 32 / 127;
        uint8_t a = (uint8_t)((k ? -16 : 16) + sw);                      /* 0 = straight up */
        int dx = (int)sin8[a] - 128, dy = -((int)cos8(a) - 128);
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++) {
                int vx = x - ox, vy = y - oy;
                int dot = vx * dx + vy * dy;
                if (dot <= 0) continue;
                int cr = iabs(vx * dy - vy * dx), lim = dot / 5;
                if (cr >= lim) continue;
                int v = level * (lim - cr) / lim;
                gfx_px_add(x, y, RGB(v * 3 / 4, v * 3 / 4, v));
            }
    }
}

/* broadcast glitch on the whole frame: shifted rows, colour split, noise. strength 0..8 */
static void glitch(uint32_t t, unsigned strength)
{
    fb_t *f = gfx_fb();
    uint32_t seed = (t / 50u + 1u) * 2654435761u;
    uint8_t row[32][3];
    for (int y = 0; y < 32; y++) {
        uint32_t h = (seed ^ (uint32_t)(y * 40503)) * 2246822519u;
        int off = (int)((h >> 28) % 5u) - 2;
        if (((h >> 20) & 7u) >= strength) off = 0;
        for (int x = 0; x < 32; x++) for (int c = 0; c < 3; c++) row[x][c] = (*f)[y][x][c];
        for (int x = 0; x < 32; x++) {
            int sr = x - off + 1, sg = x - off, sb = x - off - 1;
            (*f)[y][x][0] = (sr >= 0 && sr < 32) ? row[sr][0] : 0;
            (*f)[y][x][1] = (sg >= 0 && sg < 32) ? row[sg][1] : 0;
            (*f)[y][x][2] = (sb >= 0 && sb < 32) ? row[sb][2] : 0;
        }
    }
    for (unsigned i = 0; i < strength * 2u; i++) gfx_px((int)gfx_rnd(32), (int)gfx_rnd(32), RGB(200, 200, 200));
}

static spark_t *spark_new(void)
{
    for (int i = 0; i < NSPARK; i++) if (!X.in.s[i].life) return &X.in.s[i];
    return 0;
}

static void spark_add(int x8, int y8, int vx, int vy, uint8_t life, rgb_t c)
{
    spark_t *s = spark_new();
    if (!s) return;
    s->x = (int16_t)x8; s->y = (int16_t)y8; s->vx = (int16_t)vx; s->vy = (int16_t)vy; s->life = life;
    s->r = (uint8_t)(c >> 16); s->g = (uint8_t)(c >> 8); s->b = (uint8_t)c;
}

static void sparks_step(int gravity, uint8_t decay)
{
    for (int i = 0; i < NSPARK; i++) {
        spark_t *s = &X.in.s[i];
        if (!s->life) continue;
        s->x = (int16_t)(s->x + s->vx); s->y = (int16_t)(s->y + s->vy);
        s->vy = (int16_t)(s->vy + gravity);
        s->life = s->life > decay ? (uint8_t)(s->life - decay) : 0;
        int px = s->x >> 8, py = s->y >> 8;
        if (px < -2 || px > 33 || py > 33) { s->life = 0; continue; }
        uint8_t v = s->life > 127 ? 255 : (uint8_t)(s->life * 2);
        gfx_px(px, py, gfx_scale(RGB(s->r, s->g, s->b), v));
    }
}

/* ---- 1. intro: spotlights, letters slam in, shine, glitch, the logo blows apart (9 s loop) ---- */
#define DROP_AT(i) (1100u + (uint32_t)(i) * 150u)
#define DROP_MS    220u

void fin_intro_init(void)
{
    X.in.started = 0;
    for (int i = 0; i < NSPARK; i++) X.in.s[i].life = 0;
}

void fin_intro_frame(uint32_t now)
{
    if (!X.in.started) { X.in.started = 1; X.in.t0 = now; X.in.last = 0; X.in.landed = 0; X.in.exploded = 0; }
    uint32_t t = (now - X.in.t0) % 9000u;
    if (t < X.in.last) { X.in.landed = 0; X.in.exploded = 0; }
    X.in.last = t;

    if (t >= 6700) {                                         /* explosion and embers */
        if (!X.in.exploded) {
            X.in.exploded = 1;
            gfx_fill(0);
            draw_logo(0, 0, WHITE, RED, 30);
            fb_t *f = gfx_fb();
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 32; x++) {
                    rgb_t c = RGB((*f)[y][x][0], (*f)[y][x][1], (*f)[y][x][2]);
                    if (!c) continue;
                    int vx = (x - 16) * 22 + (int)gfx_rnd(90) - 45, vy = (y - 18) * 26 - 120 - (int)gfx_rnd(160);
                    spark_add(x << 8, y << 8, vx, vy, (uint8_t)(170 + gfx_rnd(85)), c);
                }
            gfx_fill(RGB(120, 14, 30));                      /* red flash */
            return;
        }
        gfx_fade(t < 8300 ? 150 : 110);
        sparks_step(9, 3);
        return;
    }

    gfx_fill(0);
    int level = t < 900 ? (int)t * 120 / 900 : 55;
    spotlights(t, level);

    int shake = 0;
    for (int i = 0; i < 6; i++) {
        uint32_t land = DROP_AT(i) + DROP_MS;
        if (t >= land && t < land + 90) shake = ((t - land) / 30) & 1 ? 1 : -1;
        if (t >= land && !(X.in.landed & (1u << i))) {       /* dust and chips where it hits */
            X.in.landed |= (uint8_t)(1u << i);
            for (int k = 0; k < 7; k++)
                spark_add((fin_x(i) + (int)gfx_rnd(FIN[i].w)) << 8, (LY_FIN + 6) << 8, (int)gfx_rnd(160) - 80,
                          -90 - (int)gfx_rnd(180), (uint8_t)(120 + gfx_rnd(80)), k & 1 ? RED : RGB(170, 170, 170));
        }
    }

    if (t >= 900) {
        uint32_t a = t - 900;
        draw_the(0, LY_THE + shake, gfx_scale(RED, (uint8_t)(a >= 200 ? 255 : a * 255 / 200)));
    }
    rgb_t ink = t >= 2400 ? RGB(205, 205, 212) : WHITE;
    for (int i = 0; i < 6; i++) {
        uint32_t d = DROP_AT(i);
        if (t < d) continue;
        int y = LY_FIN;
        if (t < d + DROP_MS) { int p = (int)(t - d); y = LY_FIN - 18 + 18 * p * p / (int)(DROP_MS * DROP_MS); }
        draw_letter(i, fin_x(i), y + shake, ink);
    }
    if (t >= 2000) {
        int w = t >= 2400 ? 30 : (int)(t - 2000) * 30 / 400;
        uint8_t pulse = t >= 2400 ? (uint8_t)(170 + (sin8[(uint8_t)(t / 6)] * 85) / 255) : 255;
        gfx_rect(16 - w / 2, LY_BAR + shake, w, 2, gfx_scale(RED, pulse), 1);
    }
    if (t >= 2400 && t < 6000) {                             /* shine across the letters */
        int sp = (int)((t - 2400) % 1600u);
        if (sp < 900) {
            int pos = -10 + sp * 52 / 900;
            for (int y = LY_FIN; y < LY_FIN + 7; y++)
                for (int x = 0; x < 32; x++) {
                    int band = x + (y - LY_FIN) - pos;
                    if (band < 0 || band > 2) continue;
                    if (gfx_get(x, y) == ink) gfx_px(x, y, RGB(255, 255, 255)); else gfx_px_add(x, y, RGB(22, 22, 26));
                }
        }
        if (gfx_rnd(4) == 0) {                               /* glints */
            int x = (int)gfx_rnd(32), y = LY_FIN + shake + (int)gfx_rnd(7);
            if (gfx_get(x, y) == ink) gfx_px(x, y, RGB(255, 255, 255));
        }
    }
    sparks_step(10, 4);
    if (t >= 6000) glitch(t, t < 6300 ? 3 : 7);
}

/* ---- 2. live broadcast: LIVE tag, scanline, ticker, glitch bursts ---------------------------- */
void fin_live_init(void) { X.live.started = 0; }

void fin_live_frame(uint32_t now)
{
    if (!X.live.started) { X.live.started = 1; X.live.t0 = now; }
    uint32_t t = now - X.live.t0;
    gfx_fill(0);
    for (int y = 0; y < 25; y++)                             /* moving diagonal stripes */
        for (int x = 0; x < 32; x++)
            if (((x + y + (int)(t / 90)) & 7) == 0) gfx_px(x, y, RGB(16, 2, 5));
    int sl = (int)((t / 35) % 48u);
    if (sl < 25) gfx_rect(0, sl, 32, 1, RGB(14, 14, 18), 1);

    if ((t / 600) & 1) gfx_rect(1, 1, 3, 3, RED, 1); else gfx_rect(1, 1, 3, 3, RGB(50, 8, 14), 1);
    gfx_tiny_text(6, 0, "LIVE", RGB(235, 235, 235));
    draw_the(0, 8, RED);
    draw_finals(0, 15, WHITE);

    gfx_rect(0, 25, 32, 7, RGB(150, 18, 40), 1);             /* ticker */
    static const char *const MSG = "LIVE FROM THE ARENA - THE FINALS - ";
    int w = gfx_tiny_width(MSG) + 2;
    int x = -(int)((t / 45) % (uint32_t)w);
    gfx_tiny_text(x, 26, MSG, RGB(255, 255, 255));
    gfx_tiny_text(x + w, 26, MSG, RGB(255, 255, 255));

    uint32_t g = t % 3300u;
    if (g < 180) glitch(t, g < 90 ? 6 : 3);
}

/* ---- 3. cash-out: coins rain, the cash counter runs up, CASH OUT! -------------------------- */
static void coin_reset(coin_t *c, int anywhere)
{
    c->x = (int16_t)((int)gfx_rnd(30) << 8);
    c->y = (int16_t)(anywhere ? (int)gfx_rnd(32) << 8 : -((2 + (int)gfx_rnd(24)) << 8));
    c->speed = (uint8_t)(40 + gfx_rnd(50));
    c->phase = (uint8_t)gfx_rand();
}

void fin_cash_init(void)
{
    X.cash.started = 0;
    for (int i = 0; i < NCOIN; i++) coin_reset(&X.cash.c[i], 1);
}

void fin_cash_frame(uint32_t now)
{
    if (!X.cash.started) { X.cash.started = 1; X.cash.t0 = now; }
    uint32_t t = (now - X.cash.t0) % 6500u;
    int burst = t >= 4000;
    gfx_fill(0);
    for (int i = 0; i < NCOIN; i++) {                        /* spinning coins */
        coin_t *c = &X.cash.c[i];
        c->y = (int16_t)(c->y + c->speed * (burst ? 2 : 1));
        c->phase = (uint8_t)(c->phase + 9);
        if ((c->y >> 8) > 32) coin_reset(c, 0);
        int x = c->x >> 8, y = c->y >> 8, w = iabs((int)cos8(c->phase) - 128) > 64 ? 3 : 1;
        if (w == 3) {
            gfx_rect(x, y, 3, 3, RGB(240, 165, 20), 1);
            gfx_px(x, y, RGB(90, 60, 8)); gfx_px(x + 2, y, RGB(90, 60, 8));
            gfx_px(x, y + 2, RGB(90, 60, 8)); gfx_px(x + 2, y + 2, RGB(90, 60, 8));
            gfx_px(x + 1, y + 1, RGB(255, 240, 140));
        } else {
            gfx_rect(x + 1, y, 1, 3, RGB(200, 140, 20), 1);
        }
    }
    gfx_rect(0, 1, 32, 9, 0, 1);                             /* logo, compact */
    draw_finals(0, 2, WHITE);
    gfx_rect(1, 10, 30, 1, RED, 1);

    uint32_t v = t < 4000 ? t * t / 170u : 99999u;           /* the counter */
    if (v > 99999u) v = 99999u;
    gfx_rect(0, 13, 32, 10, 0, 1);
    gfx_tiny_char(1, 15, '$', RGB(120, 230, 120));
    int digits[5] = { (int)(v / 10000u), (int)(v / 1000u % 10u), (int)(v / 100u % 10u), (int)(v / 10u % 10u), (int)(v % 10u) };
    int lead = 1;
    for (int i = 0; i < 5; i++) {
        if (digits[i] || i == 4) lead = 0;
        gfx_7seg(6 + i * 5, 14, 4, 7, 1, lead ? 10 : digits[i], burst ? RGB(255, 215, 60) : RGB(130, 240, 130));
    }
    if (burst) {
        gfx_rect(0, 24, 32, 7, 0, 1);
        rgb_t c = ((t / 200) & 1) ? RED : RGB(255, 255, 255);
        gfx_tiny_text((32 - gfx_tiny_width("CASH OUT!")) / 2, 25, "CASH OUT!", c);
    }
}

/* ================================================================================================
   THE FINALS emblem (the rounded three-legged "A", tools/gen_emblem.py) + condensed "THE FINALS"
   ================================================================================================ */
static int emb(const uint32_t *m, int w, int h, int x, int y)
{
    if ((unsigned)x >= (unsigned)w || (unsigned)y >= (unsigned)h) return 0;
    return (int)((m[y] >> (w - 1 - x)) & 1u);
}
#define EMB(x, y) emb(emblem_big, EMBLEM_BIG_W, EMBLEM_BIG_H, (x), (y))
#define EOX 2                                                /* big emblem at x 2..29 */

static rgb_t mix(rgb_t a, rgb_t b, unsigned f, unsigned n)   /* a -> b, f/n */
{
    unsigned ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
    unsigned br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
    return RGB((ar * (n - f) + br * f) / n, (ag * (n - f) + bg * f) / n, (ab * (n - f) + bb * f) / n);
}

/* chrome like the 3D logo: violet -> lavender -> white -> pink -> violet, drifting slowly */
static rgb_t chrome(int x, int y, uint32_t t)
{
    static const rgb_t STOP[6] = { RGB(70, 45, 175), RGB(150, 140, 255), RGB(235, 230, 255),
                                   RGB(235, 105, 185), RGB(115, 75, 220), RGB(70, 45, 175) };
    unsigned g = (unsigned)(x * 9 + y * 13 + 3200 + (int)(t / 45)) % 320u;
    return mix(STOP[g / 64u], STOP[g / 64u + 1], g % 64u, 64);
}

enum { EM_CHROME, EM_WHITE };

/* big emblem with its top-left at (EOX, oy); glow 0..255 = blue neon around it */
static void draw_emblem(int oy, int style, uint32_t t, uint8_t level, uint8_t glow)
{
    for (int y = -2; y < EMBLEM_BIG_H + 2; y++)
        for (int x = -2; x < EMBLEM_BIG_W + 2; x++) {
            if (!EMB(x, y)) {
                if (!glow) continue;
                int near1 = 0, near2 = 0;
                for (int dy = -2; dy <= 2; dy++)
                    for (int dx = -2; dx <= 2; dx++)
                        if (EMB(x + dx, y + dy)) { if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) near1 = 1; else near2 = 1; }
                if (near1)      gfx_px_add(EOX + x, oy + y, gfx_scale(RGB(40, 80, 255), glow));
                else if (near2) gfx_px_add(EOX + x, oy + y, gfx_scale(RGB(8, 16, 80), glow));
                continue;
            }
            rgb_t c = style == EM_WHITE ? RGB(240, 240, 250) : chrome(x, y, t);
            if (style == EM_CHROME && (!EMB(x - 1, y) || !EMB(x, y - 1))) c = mix(c, RGB(210, 220, 255), 3, 5);   /* rim light */
            gfx_px(EOX + x, oy + y, gfx_scale(c, level));
        }
}

/* "THE FINALS" in a condensed 5-row font, 31 px wide (the logo's tall narrow lettering) */
typedef struct { uint8_t w; const char *rows[5]; } glyph5_t;
static const glyph5_t COND[10] = {
    { 3, { "###", ".#.", ".#.", ".#.", ".#." } },  /* T */
    { 3, { "#.#", "#.#", "###", "#.#", "#.#" } },  /* H */
    { 2, { "##", "#.", "##", "#.", "##" } },       /* E */
    { 1, { ".", ".", ".", ".", "." } },            /* space */
    { 2, { "##", "#.", "##", "#.", "#." } },       /* F */
    { 1, { "#", "#", "#", "#", "#" } },            /* I */
    { 3, { "##.", "#.#", "#.#", "#.#", "#.#" } },  /* N */
    { 3, { ".#.", "#.#", "###", "#.#", "#.#" } },  /* A */
    { 2, { "#.", "#.", "#.", "#.", "##" } },       /* L */
    { 3, { ".##", "#..", ".#.", "..#", "##." } },  /* S */
};

static void draw_cond_text(int x, int y, rgb_t c)
{
    for (int i = 0; i < 10; i++) {
        for (int r = 0; r < 5; r++)
            for (int k = 0; k < COND[i].w; k++)
                if (COND[i].rows[r][k] == '#') gfx_px(x + k, y + r, c);
        x += COND[i].w + 1;
    }
}

static void starry_bg(uint32_t t)
{
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int v = 14 - iabs(y - 14) / 2 - iabs(x - 16) / 3;
            rgb_t c = RGB(3, 4, 10 + (v > 0 ? v : 0));
            unsigned h = (unsigned)(x * 73 + y * 151) * 2654435761u;
            if ((h >> 26) == 0) c = gfx_scale(RGB(170, 170, 230), (uint8_t)(sin8[(uint8_t)(t / 9 + (h >> 8))] / 2));
            gfx_px(x, y, c);
        }
}

static void sheen(int oy, uint32_t t, uint32_t period)      /* white band sliding over the emblem */
{
    int p = (int)(t % period);
    if (p >= 900) return;
    int pos = -12 + p * 56 / 900;
    for (int y = 0; y < EMBLEM_BIG_H; y++)
        for (int x = 0; x < EMBLEM_BIG_W; x++) {
            int b = x + y - pos;
            if (EMB(x, y) && b >= 0 && b < 3) gfx_px(EOX + x, oy + y, b == 1 ? RGB(255, 255, 255) : RGB(225, 225, 255));
        }
}

static void flare(int x, int y, uint8_t v)                   /* 4-point star */
{
    rgb_t c = gfx_scale(RGB(255, 255, 255), v), d = gfx_scale(RGB(140, 160, 255), v);
    gfx_px(x, y, c);
    gfx_px_add(x - 1, y, d); gfx_px_add(x + 1, y, d); gfx_px_add(x, y - 1, d); gfx_px_add(x, y + 1, d);
    gfx_px_add(x - 2, y, gfx_scale(d, 110)); gfx_px_add(x + 2, y, gfx_scale(d, 110));
}

void fin_em_init(void) { X.em.started = 0; }

static uint32_t em_time(uint32_t now)
{
    if (!X.em.started) { X.em.started = 1; X.em.t0 = now; }
    return now - X.em.t0;
}

/* ---- 5. chrome: the 3D logo on a starfield - chrome drift, sheen, neon glow, flares ---------- */
void fin_chrome_frame(uint32_t now)
{
    uint32_t t = em_time(now);
    starry_bg(t);
    uint8_t glow = (uint8_t)(150 + (sin8[(uint8_t)(t / 9)] * 105) / 255);
    draw_emblem(4, EM_CHROME, t, 255, glow);
    sheen(4, t, 3500);
    draw_cond_text(0, 25, RGB(215, 215, 235));
    uint32_t f = t % 5200u;
    if (f < 500) flare(EOX + 14, 4, (uint8_t)(f < 250 ? f : 500 - f));
    else if (f >= 2600 && f < 3000) flare(EOX + 25, 19, (uint8_t)(f < 2800 ? (f - 2600) : 3000 - f));
}

/* ---- 6. CRT ident: power on, white logo with red/blue fringes, scanlines, roll, power off ---- */
static void crt_logo(uint8_t level)
{
    for (int y = 0; y < EMBLEM_BIG_H; y++)                   /* colour fringes first, white core on top */
        for (int x = 0; x < EMBLEM_BIG_W; x++)
            if (EMB(x, y)) { gfx_px_add(EOX + x - 1, 3 + y, gfx_scale(RGB(140, 0, 30), level));
                             gfx_px_add(EOX + x + 1, 3 + y, gfx_scale(RGB(0, 40, 170), level)); }
    draw_cond_text(-1, 25, gfx_scale(RGB(140, 0, 30), level));
    draw_cond_text(1, 25, gfx_scale(RGB(0, 40, 170), level));
    draw_emblem(3, EM_WHITE, 0, level, 0);
    draw_cond_text(0, 25, gfx_scale(RGB(240, 240, 250), level));
}

void fin_crt_frame(uint32_t now)
{
    uint32_t t = em_time(now) % 8000u;
    gfx_fill(0);
    if (t < 450) {                                           /* power on: a line, then the picture opens */
        if (t < 200) { int w = (int)t * 16 / 200; gfx_rect(16 - w, 15, 2 * w, 2, RGB(255, 255, 255), 1); }
        else {
            crt_logo(255);
            int k = 1 + (int)(t - 200) * 15 / 250;
            for (int y = 0; y < 32; y++) if (y < 16 - k || y >= 16 + k) gfx_rect(0, y, 32, 1, 0, 1);
            gfx_rect(0, 15, 32, 2, RGB(200, 200, 220), 1);
        }
        return;
    }
    if (t >= 7500) {                                         /* power off: collapse to a line, then a dot */
        if (t < 7750) { int w = 32 - (int)(t - 7500) * 30 / 250; gfx_rect(16 - w / 2, 15, w, 2, RGB(255, 255, 255), 1); }
        else { uint8_t v = (uint8_t)(255 - (t - 7750) * 255 / 250); gfx_px(15, 15, gfx_scale(RGB(255, 255, 255), v)); gfx_px(16, 16, gfx_scale(RGB(160, 160, 255), v)); }
        return;
    }
    uint8_t flick = (uint8_t)(215 + gfx_rnd(41));
    crt_logo(flick);
    fb_t *f = gfx_fb();
    int roll = (int)((t / 22) % 48u);
    uint32_t j = t % 1700u;
    for (int y = 0; y < 32; y++) {
        int d = iabs(y - roll);
        unsigned k = (y & 1) ? 150 : 256;                    /* scanlines */
        if (d < 3) k += (unsigned)(3 - d) * 30;              /* rolling bright bar */
        for (int x = 0; x < 32; x++)
            for (int c = 0; c < 3; c++) { unsigned v = (*f)[y][x][c] * k / 256u; (*f)[y][x][c] = (uint8_t)(v > 255 ? 255 : v); }
        if (j < 120 && y >= 8 && y < 13) {                   /* tracking error: a band slips sideways */
            for (int x = 31; x >= 2; x--) for (int c = 0; c < 3; c++) (*f)[y][x][c] = (*f)[y][x - 2][c];
            gfx_rect(0, y, 2, 1, 0, 1);
        }
    }
    if (t >= 6900) glitch(t, 7);
}

/* ---- 7. neon: the outline is traced, the emblem fills up, flash, the text types in ---------- */
static int emb_edge(int x, int y) { return EMB(x, y) && (!EMB(x - 1, y) || !EMB(x + 1, y) || !EMB(x, y - 1) || !EMB(x, y + 1)); }

void fin_neon_frame(uint32_t now)
{
    uint32_t t = em_time(now) % 7200u;
    const int oy = 4;
    gfx_fill(RGB(2, 2, 8));
    uint8_t level = t < 6400 ? 255 : (uint8_t)((7200 - t) * 255 / 800);
    unsigned prog = t < 2000 ? t * 256u / 2000u : 256u;      /* outline drawn clockwise from bottom left */
    int fill = t < 2000 ? EMBLEM_BIG_H : t < 3400 ? EMBLEM_BIG_H - (int)(t - 2000) * EMBLEM_BIG_H / 1400 : 0;
    for (int y = 0; y < EMBLEM_BIG_H; y++)
        for (int x = 0; x < EMBLEM_BIG_W; x++) {
            if (!EMB(x, y)) continue;
            int sx = EOX + x, sy = oy + y;
            unsigned key = (uint8_t)(ang32[sy][sx] - 96);
            if (emb_edge(x, y)) {
                if (key < prog) {
                    rgb_t c = (prog < 256 && key + 8 > prog) ? RGB(255, 255, 255) : RGB(60, 210, 255);
                    gfx_px(sx, sy, gfx_scale(c, level));
                    gfx_px_add(sx, sy - 1, gfx_scale(RGB(10, 40, 90), level));
                }
            } else if (y >= fill) {
                rgb_t c = chrome(x, y, t);
                if (t >= 3400 && t < 3700) c = mix(RGB(255, 255, 255), c, t - 3400, 300);   /* flash */
                gfx_px(sx, sy, gfx_scale(c, level));
            }
        }
    if (t >= 3700) {
        unsigned shown = t >= 4500 ? 10u : (t - 3700) * 10u / 800u;  /* type in, letter by letter */
        int x = 0;
        for (unsigned i = 0; i < shown; i++) {
            for (int r = 0; r < 5; r++)
                for (int k = 0; k < COND[i].w; k++)
                    if (COND[i].rows[r][k] == '#') gfx_px(x + k, 25 + r, gfx_scale(RGB(60, 210, 255), level));
            x += COND[i].w + 1;
        }
        if (gfx_rnd(3) == 0) {
            int x2 = (int)gfx_rnd(EMBLEM_BIG_W), y2 = (int)gfx_rnd(EMBLEM_BIG_H);
            if (emb_edge(x2, y2)) flare(EOX + x2, oy + y2, level);
        }
    }
}

/* ---- 8. shatter: the emblem breaks into pieces that fly off, then fly back together --------- */
static void shatter_draw(int oy, uint32_t tau, uint32_t t)  /* tau = ms since the break */
{
    int i = 0;
    for (int y = 0; y < EMBLEM_BIG_H; y++)
        for (int x = 0; x < EMBLEM_BIG_W; x++) {
            if (!EMB(x, y)) continue;
            uint32_t h = (uint32_t)(++i) * 2654435761u;
            int vx = (int)((h >> 8) & 255u) - 128 + (x - 14) * 10, vy = -40 - (int)((h >> 16) & 127u) + (y - 8) * 8;
            int dx = vx * (int)tau / 4000, dy = vy * (int)tau / 4000 + 35 * (int)(tau * tau / 1000u) / 1000;
            rgb_t c = chrome(x, y, t);
            if (tau) c = gfx_scale(c, (uint8_t)(tau > 1500 ? 90 : 255 - tau * 110 / 1500));
            gfx_px(EOX + x + dx, oy + y + dy, c);
        }
}

void fin_shatter_frame(uint32_t now)
{
    uint32_t t = em_time(now) % 6800u;
    const int oy = 7;
    starry_bg(t);
    if (t < 1200) { draw_emblem(oy, EM_CHROME, t, 255, 200); sheen(oy, t, 1200); return; }
    if (t < 1450) {                                          /* cracks + shake */
        int s = ((t / 40) & 1) ? 1 : -1;
        draw_emblem(oy + s, EM_CHROME, t, 255, 200);
        for (int k = 0; k < 4; k++) {
            int x = 16, y = oy + 8;
            uint8_t a = (uint8_t)(k * 64 + 20);
            for (int n = 0; n < 9; n++) {
                x += ((int)cos8((uint8_t)(a + (n & 1 ? 20 : -20))) - 128) / 70;
                y += ((int)sin8[(uint8_t)(a + (n & 1 ? 20 : -20))] - 128) / 70;
                gfx_px(x, y + s, RGB(255, 255, 255));
            }
        }
        return;
    }
    if (t < 3050) { shatter_draw(oy, t - 1450, t); return; }             /* fly apart */
    if (t < 3350) return;                                                 /* gone */
    if (t < 4750) { shatter_draw(oy, (4750 - t) * 1600 / 1400, t); return; }   /* fly back */
    draw_emblem(oy, EM_CHROME, t, 255, 230);
    if (t < 4950) {                                                       /* snap flash */
        for (int y = 0; y < EMBLEM_BIG_H; y++)
            for (int x = 0; x < EMBLEM_BIG_W; x++)
                if (EMB(x, y)) gfx_px_add(EOX + x, oy + y, gfx_scale(RGB(200, 200, 200), (uint8_t)((4950 - t) * 255 / 200)));
    }
    sheen(oy, t - 4950, 1700);
}

/* ---- 4. spin: the logo turns around its vertical axis, front chrome, back white-on-red ------- */
void fin_spin_init(void) { X.spin.started = 0; }

void fin_spin_frame(uint32_t now)
{
    if (!X.spin.started) {                                   /* draw the logo once into our own image */
        X.spin.started = 1; X.spin.t0 = now;
        fb_t *screen = gfx_fb();
        gfx_target((fb_t *)X.spin.img);
        gfx_fill(0);
        draw_emblem(4, EM_CHROME, 0, 255, 0);
        draw_cond_text(0, 25, RGB(225, 225, 240));
        gfx_target(screen);
    }
    uint32_t t = (now - X.spin.t0) % 5000u;
    unsigned a;                                              /* 0 = front, 128 = back (in 1/256 turns) */
    if (t < 1500)      a = 0;
    else if (t < 2500) { unsigned p = (t - 1500) * 256u / 1000u; a = (p * p * (768u - 2u * p)) >> 17; }   /* 0..128 eased */
    else if (t < 4000) a = 128;
    else               { unsigned p = (t - 4000) * 256u / 1000u; a = 128 + ((p * p * (768u - 2u * p)) >> 17); }
    int c = (int)cos8((uint8_t)a) - 128;                     /* -127..127 */
    int back = c < 0, cc = iabs(c);
    uint8_t light = (uint8_t)(110 + cc * 145 / 127);

    gfx_fill(0);
    if (cc < 6) { gfx_rect(15, 5, 2, 22, RGB(120, 120, 130), 1); return; }   /* edge-on */
    for (int x = 0; x < 32; x++) {
        int num = 31 * cc + (2 * x - 31) * 127;
        if (num < 0) continue;
        int sx = num / (2 * cc);
        if (sx > 31) continue;
        for (int y = 0; y < 32; y++) {
            const uint8_t *p = X.spin.img[y][sx];
            int lit = p[0] | p[1] | p[2];
            rgb_t col;
            if (!back) col = RGB(p[0], p[1], p[2]);
            else if (y >= 2 && y < 31) col = lit ? WHITE : RGB(150, 18, 40);
            else col = 0;
            if (col) gfx_px(x, y, gfx_scale(col, light));
        }
    }
    if ((t < 1500 || (t >= 2500 && t < 4000)) && gfx_rnd(3) == 0) {   /* glints while it rests */
        int x = (int)gfx_rnd(32), y = LY_FIN + (int)gfx_rnd(7);
        if (gfx_get(x, y)) gfx_px(x, y, RGB(255, 255, 255));
    }
}
