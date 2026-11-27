/* Synthwave effects: neon mountains, neon drive, palm beach, neon city, laser tunnel, vector mountains,
 * vaporwave, VHS, laser show, cassette. Same rules as effects.c: integer maths only, frame(t) gets the time
 * in ms and is called ~40 times a second. None of them keeps state: every frame is drawn from the time. */
#include "fx_synth.h"
#include "effects.h"
#include "fx_util.h"

#define mix fx_mix

/* ---- shared pieces: sky, stars, the striped sun, mountains, the neon floor ---------------------- */
static void sky(int y0, int y1, rgb_t top, rgb_t low)
{
    int n = y1 - y0 > 1 ? y1 - y0 - 1 : 1;
    for (int y = y0; y < y1; y++) {
        rgb_t c = mix(top, low, (y - y0) * 256 / n);
        for (int x = 0; x < 32; x++) gfx_px(x, y, c);
    }
}

static void stars(int y1, uint32_t t, uint32_t density)              /* twinkling, fainter lower down */
{
    for (int y = 0; y < y1; y++)
        for (int x = 0; x < 32; x++) {
            uint32_t h = fx_hash((uint32_t)(y * 32 + x) + 977u);
            if ((h & 63u) < density)
                gfx_px_add(x, y, gfx_scale(RGB(190, 180, 255), (uint8_t)(sin8[(uint8_t)(t / 11u + (h >> 8))] * (y1 - y) / y1)));
        }
}

/* the striped sun: centre (cx2 / 2, cy2 / 2), radius r; yellow at the top to pink, gaps below its middle
   that slide down; nothing on or below row ymax */
static void sun(int cx2, int cy2, int r, uint32_t t, int ymax)
{
    for (int y = (cy2 >> 1) - r - 1; y <= (cy2 >> 1) + r + 1; y++) {
        int dy = 2 * y + 1 - cy2;
        if (y < 0 || y >= ymax) continue;
        if (dy > 0 && (int)((uint32_t)(y * 4) + 4000000u - t / 70u) % 10 < 1 + dy * 4 / (2 * r)) continue;
        for (int x = 0; x < 32; x++) {
            int dx = 2 * x + 1 - cx2;
            if (dx * dx + dy * dy <= 4 * r * r) gfx_px(x, y, mix(RGB(255, 235, 90), RGB(255, 40, 150), (dy + 2 * r) * 256 / (4 * r)));
        }
    }
}

static int peak(uint32_t k, int base, int amp, uint32_t seed)         /* odd k: a peak, even k: a pass */
{
    uint32_t h = fx_hash(k * 2654435761u + seed);
    return (k & 1u) ? base + (int)(h % (uint32_t)amp) : base - amp / 2 + (int)(h % 3u);
}

/* a mountain range on the horizon h, scrolled by `scroll` px: filled, with a neon edge */
static void ridges(int h, uint32_t scroll, int base, int amp, uint32_t seed, rgb_t fill, rgb_t edge)
{
    int prev = 0;
    for (int x = 0; x < 32; x++) {
        uint32_t u = scroll + (uint32_t)x, k = u / 6u;
        int f = (int)(u % 6u), a = peak(k, base, amp, seed), b = peak(k + 1u, base, amp, seed);
        int top = h - (a + (b - a) * f / 6);
        for (int y = top + 1; y < h; y++) gfx_px(x, y, fill);
        gfx_px(x, top, edge);
        if (x) for (int y = (prev < top ? prev : top) + 1; y < (prev < top ? top : prev); y++) gfx_px(x, y, edge);
        prev = top;
    }
}

/* the neon floor below the horizon h: lines across coming closer, lines running to the horizon */
static void grid(int h, uint32_t t, rgb_t ground, rgb_t line, unsigned speed)
{
    for (int y = h; y < 32; y++)
        for (int x = 0; x < 32; x++) gfx_px(x, y, ground);
    int off = (int)(t / speed % 16u);
    for (int k = 0; k < 16; k++) {
        int z = k * 16 + 16 - off, y = h + 1 + 128 / z;
        if (y <= 31) gfx_rect(0, y, 32, 1, gfx_scale(line, (uint8_t)(70 + (y - h) * 185 / (32 - h))), 1);
    }
    for (int i = -9; i <= 9; i++) gfx_line(16 + i, h + 1, 16 + i * 12, 31, gfx_scale(line, 150));
    gfx_rect(0, h, 32, 1, line, 1);
}

/* ---- 55: neon mountains: the sun sets behind two ranges of neon-edged mountains ---------------- */
void syn_mountains_frame(uint32_t t)
{
    const int h = 21;
    sky(0, h, RGB(18, 0, 42), RGB(210, 50, 110));
    stars(12, t, 3u);
    sun(31, 28, 9, t, h);
    ridges(h, t / 150u, 7, 6, 11u, RGB(48, 12, 80), RGB(255, 80, 200));
    ridges(h, t / 60u + 40u, 3, 4, 29u, RGB(22, 4, 44), RGB(80, 220, 255));
    grid(h, t, RGB(14, 0, 30), RGB(255, 50, 210), 5u);
}

/* ---- 56: neon drive: down a winding neon road at night, palms going by ------------------------- */
static void palm(int x, int yb, int s, int lean, int sway, rgb_t c)  /* a palm s px tall standing at (x, yb) */
{
    static const int8_t FA[6] = { -105, -75, -40, 40, 75, 105 };     /* the fronds, from straight up */
    int tx = x + lean * s / 16, ty = yb - s;
    gfx_line(x, yb, tx, ty, c);
    if (s >= 10) gfx_line(x + 1, yb, tx + 1, ty + 1, c);
    if (s < 3) return;
    int len = s / 2 + 1;
    for (int k = 0; k < 6; k++) {
        int a = FA[k] + sway, mx = tx + SN(a) * len / 200, my = ty - CS(a) * len / 200;
        gfx_line(tx, ty, mx, my, c);
        gfx_line(mx, my, tx + SN(a) * len / 128, ty - CS(a) * len / 128 + len / 3, c);   /* drooping tips */
    }
}

static void car(int cx)                                               /* a sports car from behind */
{
    const rgb_t body = RGB(70, 15, 95), dark = RGB(35, 8, 50), tyre = RGB(5, 5, 8);
    gfx_rect(cx - 4, 23, 8, 1, dark, 1);
    gfx_rect(cx - 5, 24, 10, 1, RGB(90, 60, 160), 1);
    gfx_px(cx - 3, 24, RGB(180, 150, 235));
    gfx_rect(cx - 6, 25, 12, 1, body, 1);
    gfx_rect(cx - 7, 26, 14, 2, body, 1);
    gfx_rect(cx - 6, 26, 12, 1, RGB(255, 30, 70), 1);                  /* the tail lights */
    gfx_rect(cx - 7, 28, 14, 1, dark, 1);
    gfx_rect(cx - 2, 28, 4, 1, RGB(190, 190, 200), 1);
    gfx_rect(cx - 7, 29, 3, 2, tyre, 1); gfx_rect(cx + 4, 29, 3, 2, tyre, 1);
    for (int x = cx - 8; x <= cx + 7; x++) gfx_px_add(x, 31, RGB(90, 10, 30));
}

void syn_drive_frame(uint32_t t)
{
    const int h = 13;
    sky(0, h, RGB(20, 0, 45), RGB(255, 80, 120));
    stars(9, t, 3u);
    sun(31, 22, 7, t, h);
    ridges(h, t / 400u, 2, 3, 5u, RGB(40, 10, 70), RGB(255, 90, 200));
    int bend = SN((int)(t / 60u));                                    /* the road bends up to 8 px at the horizon */
    uint32_t run = t / 6u;
    for (int y = h; y < 32; y++) {
        int dy = y - h + 1, z = 320 / dy, xc = 248 + bend * (19 - dy) / (19 * dy), hw = dy * 14 + 8;   /* 1/16 px */
        uint32_t zz = (uint32_t)z + run;
        int dash = (int)((zz / 20u) & 1u);
        for (int x = 0; x < 32; x++) {
            int d = x * 16 + 8 - xc;
            rgb_t c;
            if (d < 0) d = -d;
            if (d < hw - 14) c = (d < 6 + dy / 2 && dash) ? RGB(255, 225, 120) : RGB(26, 10, 42);
            else if (d < hw) c = RGB(255, 60, 200);
            else if (d < hw + 8 + dy * 3) c = dash ? RGB(255, 60, 140) : RGB(220, 220, 235);
            else c = zz % 40u < 3u ? RGB(110, 20, 150) : RGB(16, 0, 32);
            gfx_px(x, y, c);
        }
    }
    int pz[4], order[4] = { 0, 1, 2, 3 };
    for (int p = 0; p < 4; p++) pz[p] = 300 - (int)((run + (uint32_t)p * 75u) % 300u);
    for (int i = 0; i < 4; i++)                                         /* far ones first */
        for (int j = i + 1; j < 4; j++)
            if (pz[order[j]] > pz[order[i]]) { int s = order[i]; order[i] = order[j]; order[j] = s; }
    for (int i = 0; i < 4; i++) {
        int p = order[i], z = pz[p];
        if (z < 17) continue;
        int dy = 320 / z, side = (p & 1) ? 1 : -1;
        int xc = 248 + bend * (19 - dy) / (19 * dy), hw = dy * 14 + 8;
        palm((xc + side * (hw + 24 + dy * 12)) / 16, h + dy - 1, dy * 26 / 19 + 1, -side * 3, SN((int)(t / 20u)) * 8 / 128, RGB(50, 10, 80));
    }
    car(16 - bend / 64);
}

/* ---- 57: palm beach: the sun goes down into the sea, palms in the wind -------------------------- */
static void big_palm(int bx, int by, int tx, int ty, int dir, uint32_t t, rgb_t c)
{
    int n = by - ty;
    for (int i = 0; i <= n; i++) {                                      /* the trunk bends more near the top */
        int x = bx + (tx - bx) * i * i / (n * n);
        gfx_px(x, by - i, c);
        if (i < n / 2) gfx_px(x + 1, by - i, c);
    }
    for (int k = 0; k < 5; k++) {                                       /* five fronds, swaying */
        int a = -90 + k * 45 + SN((int)(t / 25u) + k * 30) * 6 / 128 + dir * 8, len = k == 2 ? 5 : 8;
        for (int i = 1; i <= len; i++) gfx_px(tx + SN(a) * i / 128, ty - CS(a) * i / 128 + i * i / 10, c);
    }
    gfx_rect(tx - 1, ty - 1, 3, 2, c, 1);
}

void syn_beach_frame(uint32_t t)
{
    const int h = 18;
    sky(0, h, RGB(40, 8, 85), RGB(255, 120, 80));
    stars(6, t, 2u);
    sun(31, 30, 8, t, h);
    for (int y = h; y < 32; y++) {                                      /* the sea, the sun's path on it in bands */
        int dy = y - h, w = 14 - dy / 2 + SN((int)(t / 5u) + y * 50) * 4 / 128, wob = SN((int)(t / 7u) + y * 37) * 3 / 128;
        rgb_t sea = mix(RGB(70, 20, 100), RGB(16, 4, 40), dy * 256 / 14), glow = mix(RGB(255, 200, 90), RGB(255, 60, 140), dy * 256 / 14);
        int lit = (int)((uint32_t)(y * 5) + t / 150u) % 4 != 0;
        for (int x = 0; x < 32; x++) {
            int dx = x * 2 - 30 - wob;
            if (dx < 0) dx = -dx;
            int k = 210 - dy * 10 - dx * 6;
            gfx_px(x, y, lit && dx < w && k > 0 ? mix(sea, glow, k) : sea);
        }
    }
    gfx_rect(0, h, 32, 1, RGB(255, 140, 120), 1);
    for (int k = 0; k < 2; k++) {                                       /* two birds */
        int bx = (int)((t / 80u + (uint32_t)k * 21u) % 48u) - 8, by = 5 + k * 3 + SN((int)(t / 30u) + k * 60) / 100;
        int up = (int)(((t / 160u) + (uint32_t)k) & 1u);
        gfx_px(bx, by, RGB(30, 5, 40)); gfx_px(bx - 1, by - up, RGB(30, 5, 40)); gfx_px(bx + 1, by - up, RGB(30, 5, 40));
    }
    big_palm(3, 31, 9, 7, -1, t, RGB(14, 0, 22));
    big_palm(30, 31, 25, 14, 1, t + 700u, RGB(14, 0, 22));
}

/* ---- 58: neon city: towers with lit windows and searchlights, mirrored in the water ------------- */
static void beam(int x0, int y0, int a, rgb_t c)                       /* a faint searchlight, a = 0 is up */
{
    for (int i = 2; i < 40; i++) {
        int x = x0 + SN(a) * i / 128, y = y0 - CS(a) * i / 128;
        if (y < 0) break;
        for (int w = -(i / 14); w <= i / 14; w++) gfx_px_add(x + w, y, c);
    }
}

void syn_city_frame(uint32_t t)
{
    const int h = 22;
    sky(0, h, RGB(8, 0, 28), RGB(110, 25, 120));
    stars(10, t, 3u);
    gfx_circle(24, 6, 3, RGB(255, 150, 210), 1);
    gfx_circle(25, 5, 2, RGB(30, 6, 58), 1);                            /* a crescent */
    for (int k = 0; k < 2; k++) beam(k ? 22 : 9, h, (k ? 20 : -20) + SN((int)(t / 30u) + k * 90) * 28 / 128, RGB(36, 22, 64));
    for (int x = 0, i = 0; x < 32; i++) {
        uint32_t hh = fx_hash((uint32_t)i * 7919u + 3u);
        int w = 3 + (int)(hh % 3u), ht = i == 3 ? 18 : 5 + (int)((hh >> 4) % 12u), top = h - ht;
        rgb_t wall = (i & 1) ? RGB(18, 6, 38) : RGB(26, 8, 50), win = ((hh >> 8) & 1u) ? RGB(255, 200, 90) : RGB(90, 220, 255);
        gfx_rect(x, top, w, ht, wall, 1);
        for (int wy = top + 1; wy < h - 1; wy += 2)
            for (int wx = x + 1; wx < x + w - 1; wx++) {
                uint32_t ph = fx_hash((uint32_t)(wx * 37 + wy * 101));   /* each window on and off in its own time */
                if (((t / 300u + (ph & 255u)) / 40u + (ph >> 8)) % 3u) gfx_px(wx, wy, win);
            }
        if (i == 3 && ((t / 600u) & 1u)) gfx_px(x + w / 2, top - 1, RGB(255, 30, 30));
        if (i == 5 && (t / 300u) % 7u) gfx_rect(x, top + 2, w, 3, RGB(255, 60, 200), 0);   /* a neon sign that blinks */
        x += w + (int)((hh >> 12) & 1u);
    }
    for (int y = h; y < 32; y++) {                                      /* the water */
        int src = 2 * h - 1 - y, wob = SN((int)(t / 5u) + y * 45) / 100;
        for (int x = 0; x < 32; x++) {
            int sx = x + wob < 0 ? 0 : x + wob > 31 ? 31 : x + wob;
            rgb_t c = gfx_scale(gfx_get(sx, src), 110);
            if ((x + y * 3 + (int)(t / 120u)) % 9 == 0) c = mix(c, RGB(255, 80, 200), 70);
            gfx_px(x, y, mix(c, RGB(10, 0, 40), 60));
        }
    }
    gfx_rect(0, h, 32, 1, RGB(255, 60, 200), 1);
}

/* ---- 59: laser tunnel: neon squares rushing past, twisting -------------------------------------- */
void syn_tunnel_frame(uint32_t t)
{
    const int D = 24, N = 14;
    int off = (int)(t / 5u % (uint32_t)D), passed = (int)(t / 5u / (uint32_t)D);
    int cx = 248 + SN((int)(t / 37u)) * 40 / 128, cy = 248 + CS((int)(t / 29u)) * 32 / 128;   /* 1/16 px */
    int px[4] = { 0 }, py[4] = { 0 }, qx[4], qy[4];
    gfx_fill(RGB(6, 0, 14));
    for (int k = N - 1; k >= 0; k--) {
        int z = k * D + D - off, s = 4000 / z, a = (int)(t / 30u) + z / 3;
        rgb_t c = ((k + passed) & 1) ? RGB(255, 50, 200) : RGB(60, 220, 255);
        c = gfx_scale(c, (uint8_t)(255 - z * 200 / (N * D)));
        for (int j = 0; j < 4; j++) {
            int ang = a + j * 64 + 32;
            qx[j] = (cx + SN(ang) * s * 181 / 16384) / 16;
            qy[j] = (cy - CS(ang) * s * 181 / 16384) / 16;
        }
        for (int j = 0; j < 4; j++) gfx_line(qx[j], qy[j], qx[(j + 1) & 3], qy[(j + 1) & 3], c);
        if (k < N - 1) for (int j = 0; j < 4; j++) gfx_line(px[j], py[j], qx[j], qy[j], gfx_scale(c, 110));
        for (int j = 0; j < 4; j++) { px[j] = qx[j]; py[j] = qy[j]; }
    }
}

/* ---- 60: vector mountains: flying down a wireframe valley towards the sun ----------------------- */
static int vnoise(int x, int z)                                        /* smooth noise 0..255, 16 unit lattice */
{
    int ix = x >> 4, iz = z >> 4, fx = x & 15, fz = z & 15;
    int a = (int)(fx_hash((uint32_t)(ix * 7919 + iz * 104729)) & 255u), b = (int)(fx_hash((uint32_t)((ix + 1) * 7919 + iz * 104729)) & 255u);
    int c = (int)(fx_hash((uint32_t)(ix * 7919 + (iz + 1) * 104729)) & 255u), d = (int)(fx_hash((uint32_t)((ix + 1) * 7919 + (iz + 1) * 104729)) & 255u);
    int ab = a + (b - a) * fx / 16, cd = c + (d - c) * fx / 16;
    return ab + (cd - ab) * fz / 16;
}

void syn_vector_frame(uint32_t t)
{
    const int h = 12, F = 24, CAM = 22, S = 16, K = 14;
    uint32_t run = t / 8u;
    int off = (int)(run % (uint32_t)S), base = (int)((run - (uint32_t)off) % 65536u);
    sky(0, h + 1, RGB(10, 0, 30), RGB(130, 25, 110));
    stars(8, t, 3u);
    sun(31, 20, 6, t, h + 1);
    for (int y = h + 1; y < 32; y++) gfx_rect(0, y, 32, 1, 0, 1);
    for (int k = K - 1; k >= 0; k--) {                                  /* rows of the land, far ones first */
        int z = k * S + S - off + 8, wz = base + k * S, prev = 0;
        rgb_t c = gfx_scale(mix(RGB(255, 50, 190), RGB(70, 235, 255), (K - 1 - k) * 256 / (K - 1)), (uint8_t)(110 + (K - k) * 145 / K));
        for (int x = 0; x < 32; x++) {
            int wx = (x * 2 - 31) * z / (2 * F), ax = wx < 0 ? -wx : wx;
            int n = vnoise(wx * 2 + 512, wz), ht = (ax > 12 ? (ax - 12) / 2 : 0) + n * n / 256 * (ax + 6) / 220;   /* peaks */
            int ys = h + 1 + (CAM - ht) * F / z;
            if (ys < 0) ys = 0;
            if (ys > 40) ys = 40;
            for (int y = ys + 1; y < 32; y++) gfx_px(x, y, 0);          /* hides what is behind */
            gfx_px(x, ys, c);
            if (x) for (int y = (prev < ys ? prev : ys) + 1; y < (prev < ys ? ys : prev); y++) gfx_px(x, y, c);
            prev = ys;
        }
    }
}

/* ---- 61: vaporwave: a wire shape turning over a chequered floor in pastel haze ------------------ */
void syn_vapor_frame(uint32_t t)
{
    static const int8_t V[6][3] = { { 0, 8, 0 }, { 0, -8, 0 }, { 8, 0, 0 }, { -8, 0, 0 }, { 0, 0, 8 }, { 0, 0, -8 } };
    static const uint8_t E[12][2] = { { 0, 2 }, { 0, 3 }, { 0, 4 }, { 0, 5 }, { 1, 2 }, { 1, 3 }, { 1, 4 }, { 1, 5 },
                                      { 2, 4 }, { 4, 3 }, { 3, 5 }, { 5, 2 } };
    const int h = 19;
    uint32_t run = t / 5u;
    sky(0, h, RGB(40, 10, 80), RGB(255, 150, 200));
    stars(8, t, 2u);
    for (int y = h; y < 32; y++) {
        int z = 256 / (y - h + 1);
        for (int x = 0; x < 32; x++) {
            int wx = (x * 2 - 31) * z / 32 + 4096;
            rgb_t c = (((wx / 12) + (int)(((uint32_t)z + run) / 12u)) & 1) ? RGB(255, 80, 190) : RGB(40, 200, 210);
            gfx_px(x, y, mix(c, RGB(255, 150, 200), z * 200 / 256));
        }
    }
    int bob = SN((int)(t / 9u)) * 2 / 128, ay = (int)(t / 18u), ax = 24 + SN((int)(t / 40u)) / 8, sx[6], sy[6];
    for (int x = 12; x <= 19; x++) gfx_px(x, h + 3, gfx_scale(gfx_get(x, h + 3), 140));   /* its shadow */
    for (int x = 13; x <= 18; x++) gfx_px(x, h + 4, gfx_scale(gfx_get(x, h + 4), 170));
    for (int i = 0; i < 6; i++) {
        int x1 = (V[i][0] * CS(ay) - V[i][2] * SN(ay)) / 128, z1 = (V[i][0] * SN(ay) + V[i][2] * CS(ay)) / 128;
        int y2 = (V[i][1] * CS(ax) - z1 * SN(ax)) / 128, z2 = (V[i][1] * SN(ax) + z1 * CS(ax)) / 128;
        int p = 256 * 40 / (40 + z2);
        sx[i] = 16 + x1 * p / 256; sy[i] = 9 + bob - y2 * p / 256;
    }
    for (int e = 0; e < 12; e++) gfx_line(sx[E[e][0]], sy[E[e][0]], sx[E[e][1]], sy[E[e][1]], RGB(120, 255, 250));
    for (int i = 0; i < 6; i++) gfx_px(sx[i], sy[i], RGB(255, 255, 255));
}

/* ---- 62: VHS: the neon mountains on a worn tape: bleeding colour, a rolling band, PLAY ----------- */
void syn_vhs_frame(uint32_t t)
{
    syn_mountains_frame(t);
    fb_t *f = gfx_fb();
    int band = (int)(t / 35u % 60u) - 12;
    for (int y = 0; y < 32; y++) {
        for (int x = 31; x > 0; x--) (*f)[y][x][0] = (*f)[y][x - 1][0];    /* red runs a pixel to the right */
        int in = y >= band && y < band + 4;
        uint32_t h = fx_hash(t / 50u * 64u + (uint32_t)y);
        int shift = in ? 1 + (int)(h % 3u) : (h & 31u) == 0;
        for (int x = 31; x >= 0; x--)
            for (int c = 0; c < 3; c++) (*f)[y][x][c] = x >= shift ? (*f)[y][x - shift][c] : (uint8_t)(40 + (h >> 20) % 60u);
        if (in)
            for (int x = 0; x < 32; x++)
                if ((fx_hash(h + (uint32_t)x) & 3u) == 0) gfx_px_add(x, y, RGB(60, 60, 70));
        if (y & 1)
            for (int x = 0; x < 32; x++)
                for (int c = 0; c < 3; c++) (*f)[y][x][c] = (uint8_t)((*f)[y][x][c] * 4 / 5);
    }
    for (uint32_t k = 0; k < 5u; k++) {                                 /* snow */
        uint32_t h = fx_hash(t / 40u * 8u + k);
        gfx_px((int)(h % 32u), (int)((h >> 8) % 32u), RGB(200, 200, 210));
    }
    if ((t / 700u) % 4u != 3u) {
        gfx_tiny_text(1, 1, "PLAY", RGB(235, 235, 235));
        for (int i = 0; i < 3; i++) gfx_rect(18 + i, 1 + i, 1, 5 - 2 * i, RGB(235, 235, 235), 1);
    }
}

/* ---- 63: laser show: two projectors sweep fans of beams over the floor -------------------------- */
static void ray(int x0, int y0, int a, rgb_t c)                        /* an additive beam, a = 0 is up, fading */
{
    for (int i = 1; i < 48; i++) {
        int x = x0 + SN(a) * i / 128, y = y0 - CS(a) * i / 128;
        if (x < -1 || x > 32 || y < -1) break;
        gfx_px_add(x, y, gfx_scale(c, (uint8_t)(255 - i * 4)));
    }
}

void syn_laser_frame(uint32_t t)
{
    static const rgb_t COL[4] = { RGB(255, 40, 160), RGB(40, 220, 255), RGB(120, 255, 90), RGB(190, 70, 255) };
    const int h = 25;
    sky(0, h, RGB(4, 0, 12), RGB(34, 8, 50));
    grid(h, t, RGB(10, 0, 22), RGB(200, 40, 200), 8u);
    uint8_t beat = (t / 250u) & 1u ? 255 : 170;
    rgb_t c1 = gfx_scale(COL[(t / 4000u) % 4u], beat), c2 = gfx_scale(COL[(t / 4000u + 1u) % 4u], beat);
    int sweep = SN((int)(t / 22u)) * 36 / 128, spread = 15 + SN((int)(t / 53u)) * 8 / 128;
    for (int i = -3; i <= 3; i++) {
        ray(6, h, sweep + i * spread + 20, c1);
        ray(25, h, -sweep + i * spread - 20, c2);
    }
    gfx_px(6, h, RGB(255, 255, 255)); gfx_px(25, h, RGB(255, 255, 255));
}

/* ---- 64: cassette: a neon tape, its reels turning, the tape running from one to the other ------- */
static void disc(int cx2, int cy2, int r4, rgb_t c, int y0, int y1)   /* a filled disc, centre /2, radius /4, rows y0..y1 */
{
    for (int y = y0; y <= y1; y++)
        for (int x = (cx2 - r4 / 2) / 2 - 1; x <= (cx2 + r4 / 2) / 2 + 1; x++) {
            int dx = (2 * x + 1 - cx2) * 2, dy = (2 * y + 1 - cy2) * 2;
            if (dx * dx + dy * dy <= r4 * r4) gfx_px(x, y, c);
        }
}

void syn_cassette_frame(uint32_t t)
{
    const rgb_t bg = RGB(14, 0, 28);
    rgb_t edge = gfx_scale(RGB(60, 230, 255), (uint8_t)(200 + SN((int)(t / 12u)) * 55 / 128));
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) gfx_px(x, y, (x % 4 == 0 || y % 4 == 0) ? RGB(26, 4, 46) : bg);
    gfx_rect(2, 7, 28, 19, RGB(24, 6, 44), 1);
    gfx_rect(2, 7, 28, 19, edge, 0);
    gfx_px(2, 7, bg); gfx_px(29, 7, bg); gfx_px(2, 25, bg); gfx_px(29, 25, bg);
    gfx_rect(4, 9, 24, 3, RGB(255, 60, 160), 1);                         /* the label */
    gfx_rect(4, 12, 24, 5, RGB(255, 150, 60), 1);
    gfx_rect(4, 17, 24, 2, RGB(255, 220, 90), 1);
    gfx_tiny_char(4, 12, 'A', RGB(60, 10, 40));
    gfx_rect(8, 12, 16, 5, RGB(10, 2, 20), 1);                           /* the window */
    uint32_t cyc = t % 24000u;
    int rl = 18 - (int)(cyc * 12u / 24000u), rr = 6 + (int)(cyc * 12u / 24000u);   /* the tape on each reel, 1/4 px */
    disc(23, 29, rl, RGB(110, 60, 35), 12, 16);
    disc(41, 29, rr, RGB(110, 60, 35), 12, 16);
    for (int r = 0; r < 2; r++) {
        int cx = r ? 20 : 11, a = (int)(t / 5u) + r * 40;
        gfx_rect(cx, 14, 2, 2, RGB(230, 230, 240), 1);
        for (int s = 0; s < 3; s++) {
            int sa = a + s * 85;
            gfx_px(cx + (SN(sa) >= 0) + SN(sa) * 2 / 128, 14 + (CS(sa) < 0) - CS(sa) * 2 / 128, RGB(255, 255, 255));
        }
    }
    gfx_line(8, 25, 10, 21, edge); gfx_line(23, 25, 21, 21, edge); gfx_line(10, 21, 21, 21, edge);
    gfx_px(12, 23, edge); gfx_px(19, 23, edge); gfx_rect(15, 23, 2, 1, edge, 1);
    gfx_px(4, 23, RGB(150, 150, 170)); gfx_px(27, 23, RGB(150, 150, 170));
    gfx_px(4, 8, RGB(150, 150, 170)); gfx_px(27, 8, RGB(150, 150, 170));
}
