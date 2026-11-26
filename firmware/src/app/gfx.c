#include "gfx.h"
#include "tables.h"

static fb_t *fb;
static uint32_t rng = 0x12345678u;
static int clip_x0, clip_y0, clip_x1 = GFX_W, clip_y1 = GFX_H;

void  gfx_target(fb_t *f) { fb = f; gfx_noclip(); }
fb_t *gfx_fb(void)        { return fb; }

void gfx_clip(int x0, int y0, int x1, int y1)
{
    clip_x0 = x0 < 0 ? 0 : x0; clip_y0 = y0 < 0 ? 0 : y0;
    clip_x1 = x1 > GFX_W ? GFX_W : x1; clip_y1 = y1 > GFX_H ? GFX_H : y1;
}

void gfx_noclip(void) { clip_x0 = 0; clip_y0 = 0; clip_x1 = GFX_W; clip_y1 = GFX_H; }

#define OUTSIDE(x, y) ((x) < clip_x0 || (x) >= clip_x1 || (y) < clip_y0 || (y) >= clip_y1)

void gfx_fill(rgb_t c)
{
    uint8_t r = (uint8_t)(c >> 16), g = (uint8_t)(c >> 8), b = (uint8_t)c;
    for (int y = 0; y < GFX_H; y++)
        for (int x = 0; x < GFX_W; x++) { (*fb)[y][x][0] = r; (*fb)[y][x][1] = g; (*fb)[y][x][2] = b; }
}

void gfx_fade(uint8_t keep)
{
    uint8_t *p = &(*fb)[0][0][0];
    for (int i = 0; i < GFX_W * GFX_H * 3; i++) p[i] = (uint8_t)((p[i] * keep) >> 8);
}

void gfx_px(int x, int y, rgb_t c)
{
    if (OUTSIDE(x, y)) return;
    (*fb)[y][x][0] = (uint8_t)(c >> 16); (*fb)[y][x][1] = (uint8_t)(c >> 8); (*fb)[y][x][2] = (uint8_t)c;
}

static uint8_t sat(unsigned v) { return v > 255u ? 255u : (uint8_t)v; }

void gfx_px_add(int x, int y, rgb_t c)
{
    if (OUTSIDE(x, y)) return;
    uint8_t *p = (*fb)[y][x];
    p[0] = sat(p[0] + ((c >> 16) & 255u)); p[1] = sat(p[1] + ((c >> 8) & 255u)); p[2] = sat(p[2] + (c & 255u));
}

rgb_t gfx_get(int x, int y)
{
    if ((unsigned)x >= GFX_W || (unsigned)y >= GFX_H) return 0;
    return RGB((*fb)[y][x][0], (*fb)[y][x][1], (*fb)[y][x][2]);
}

void gfx_line(int x0, int y0, int x1, int y1, rgb_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        gfx_px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_rect(int x, int y, int w, int h, rgb_t c, int fill)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            if (fill || j == 0 || j == h - 1 || i == 0 || i == w - 1) gfx_px(x + i, y + j, c);
}

void gfx_circle(int cx, int cy, int r, rgb_t c, int fill)
{
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++) {
            int d = x * x + y * y;
            if (d <= r * r + r && (fill || d >= (r - 1) * (r - 1) + (r - 1))) gfx_px(cx + x, cy + y, c);
        }
}

rgb_t gfx_hsv(uint8_t h, uint8_t s, uint8_t v)
{
    unsigned region = h / 43u, rem = (h - region * 43u) * 6u;
    unsigned p = (v * (255u - s)) >> 8;
    unsigned q = (v * (255u - ((s * rem) >> 8))) >> 8;
    unsigned t = (v * (255u - ((s * (255u - rem)) >> 8))) >> 8;
    switch (region) {
    case 0:  return RGB(v, t, p);
    case 1:  return RGB(q, v, p);
    case 2:  return RGB(p, v, t);
    case 3:  return RGB(p, q, v);
    case 4:  return RGB(t, p, v);
    default: return RGB(v, p, q);
    }
}

rgb_t gfx_scale(rgb_t c, uint8_t v)
{
    return RGB((((c >> 16) & 255u) * v) / 255u, (((c >> 8) & 255u) * v) / 255u, ((c & 255u) * v) / 255u);
}

int gfx_char(int x, int y, char ch, rgb_t c)
{
    if (ch < 32 || ch > 126) ch = '?';
    if (ch == ' ') return 3;
    const uint8_t *g = font5x7[ch - 32];
    int first = 0, last = 4;
    while (first < 4 && g[first] == 0) first++;
    while (last > first && g[last] == 0) last--;
    for (int col = first; col <= last; col++)
        for (int row = 0; row < 7; row++)
            if (g[col] & (1u << row)) gfx_px(x + col - first, y + row, c);
    return last - first + 2;
}

int gfx_text(int x, int y, const char *s, rgb_t c)
{
    while (*s) x += gfx_char(x, y, *s++, c);
    return x;
}

int gfx_text_width(const char *s)
{
    int w = 0;
    for (; *s; s++) {
        char ch = (*s < 32 || *s > 126) ? '?' : *s;
        if (ch == ' ') { w += 3; continue; }
        const uint8_t *g = font5x7[ch - 32];
        int first = 0, last = 4;
        while (first < 4 && g[first] == 0) first++;
        while (last > first && g[last] == 0) last--;
        w += last - first + 2;
    }
    return w ? w - 1 : 0;
}

/* 3x5 glyphs, 15 bits: row 0 (top) in bits 14..12, left pixel = highest bit of the row */
#define G(a, b, c, d, e) (uint16_t)(((a) << 12) | ((b) << 9) | ((c) << 6) | ((d) << 3) | (e))
static const uint16_t TINY_DIG[10] = {
    G(7,5,5,5,7), G(2,6,2,2,7), G(7,1,7,4,7), G(7,1,7,1,7), G(5,5,7,1,1),
    G(7,4,7,1,7), G(7,4,7,5,7), G(7,1,1,2,2), G(7,5,7,5,7), G(7,5,7,1,7),
};
static const uint16_t TINY_AZ[26] = {
    G(2,5,7,5,5), G(6,5,6,5,6), G(3,4,4,4,3), G(6,5,5,5,6), G(7,4,6,4,7), G(7,4,6,4,4),
    G(3,4,5,5,3), G(5,5,7,5,5), G(7,2,2,2,7), G(1,1,1,5,2), G(5,5,6,5,5), G(4,4,4,4,7),
    G(5,7,7,5,5), G(6,5,5,5,5), G(2,5,5,5,2), G(6,5,6,4,4), G(2,5,5,6,3), G(6,5,6,5,5),
    G(3,4,2,1,6), G(7,2,2,2,2), G(5,5,5,5,7), G(5,5,5,5,2), G(5,5,7,7,5), G(5,5,2,5,5),
    G(5,5,2,2,2), G(7,1,2,4,7),
};

static uint16_t tiny_glyph(char ch)
{
    if (ch >= '0' && ch <= '9') return TINY_DIG[ch - '0'];
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
    if (ch >= 'A' && ch <= 'Z') return TINY_AZ[ch - 'A'];
    switch (ch) {
    case ':':  return G(0,2,0,2,0);
    case '.':  return G(0,0,0,0,2);
    case '-':  return G(0,0,7,0,0);
    case '!':  return G(2,2,2,0,2);
    case '?':  return G(6,1,2,0,2);
    case '\'': return G(2,2,0,0,0);
    case '/':  return G(1,1,2,4,4);
    case '%':  return G(5,1,2,4,5);
    case '+':  return G(0,2,7,2,0);
    case '$':  return G(3,6,2,3,6);
    default:   return 0;
    }
}
#undef G

uint16_t gfx_tiny_glyph(char ch) { return tiny_glyph(ch); }

static int tiny_cols(uint16_t g, int *first)      /* used columns of a glyph (0..2) */
{
    int lo = 3, hi = -1;
    for (int col = 0; col < 3; col++)
        for (int row = 0; row < 5; row++)
            if (g & (1u << (14 - row * 3 - col))) { if (col < lo) lo = col; if (col > hi) hi = col; }
    *first = lo;
    return hi < lo ? 0 : hi - lo + 1;
}

int gfx_tiny_char(int x, int y, char ch, rgb_t c)
{
    uint16_t g = tiny_glyph(ch);
    int first, w = tiny_cols(g, &first);
    if (!w) return 2;                                   /* space / unknown */
    for (int row = 0; row < 5; row++)
        for (int col = first; col < first + w; col++)
            if (g & (1u << (14 - row * 3 - col))) gfx_px(x + col - first, y + row, c);
    return w + 1;
}

int gfx_tiny_text(int x, int y, const char *s, rgb_t c)
{
    while (*s) x += gfx_tiny_char(x, y, *s++, c);
    return x;
}

int gfx_tiny_width(const char *s)
{
    int w = 0, first;
    for (; *s; s++) { int n = tiny_cols(tiny_glyph(*s), &first); w += n ? n + 1 : 2; }
    return w ? w - 1 : 0;
}

/* segments: a top, b top-right, c bottom-right, d bottom, e bottom-left, f top-left, g middle */
static const uint8_t SEG[12] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x00, 0x40 };

void gfx_7seg(int x, int y, int w, int h, int th, int digit, rgb_t c)
{
    if (digit < 0 || digit > 11) return;
    uint8_t s = SEG[digit];
    int mid = y + (h - th) / 2;
    if (s & 0x01) gfx_rect(x, y, w, th, c, 1);
    if (s & 0x40) gfx_rect(x, mid, w, th, c, 1);
    if (s & 0x08) gfx_rect(x, y + h - th, w, th, c, 1);
    if (s & 0x20) gfx_rect(x, y, th, mid - y + th, c, 1);
    if (s & 0x02) gfx_rect(x + w - th, y, th, mid - y + th, c, 1);
    if (s & 0x10) gfx_rect(x, mid, th, y + h - mid, c, 1);
    if (s & 0x04) gfx_rect(x + w - th, mid, th, y + h - mid, c, 1);
}

int gfx_7seg_hit(int px, int py, int w, int h, int th, int digit)
{
    if (digit < 0 || digit > 11 || px < 0 || py < 0 || px >= w || py >= h) return 0;
    uint8_t s = SEG[digit];
    int mid = (h - th) / 2;
    if ((s & 0x01) && py < th) return 1;
    if ((s & 0x40) && py >= mid && py < mid + th) return 1;
    if ((s & 0x08) && py >= h - th) return 1;
    if ((s & 0x20) && px < th && py < mid + th) return 1;
    if ((s & 0x02) && px >= w - th && py < mid + th) return 1;
    if ((s & 0x10) && px < th && py >= mid) return 1;
    if ((s & 0x04) && px >= w - th && py >= mid) return 1;
    return 0;
}

void gfx_seed(uint32_t s) { rng = s ? s : 0x12345678u; }

uint32_t gfx_rand(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

uint32_t gfx_rnd(uint32_t n) { return n ? gfx_rand() % n : 0; }
