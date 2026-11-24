/* Text engine - see textfx.h. Effects after MD_Parola (scroll, wipe, opening/closing, blinds, dissolve, mesh,
 * slice, grow, fade) plus drop, zoom, rain/melt, explode, laser, flip and matrix; hold effects; colour modes;
 * shadow/outline/glow/rails; any animation as a dimmed background (drawn in its own frame buffer so effects
 * with trails keep working); live time/date tokens in the panel font. Integer maths only. */
#include "textfx.h"
#include "gfx.h"
#include "effects.h"
#include "clockface.h"
#include "tables.h"

enum { K_STRIP, K_FONT };
enum { FX_NONE, FX_SCROLL_L, FX_SCROLL_R, FX_SCROLL_U, FX_SCROLL_D, FX_TYPE, FX_WIPE, FX_OPEN, FX_CLOSE, FX_BLINDS,
       FX_DISSOLVE, FX_MESH, FX_SLICE, FX_DROP, FX_GROW_U, FX_GROW_D, FX_ZOOM, FX_FADE, FX_RAIN, FX_EXPLODE,
       FX_LASER, FX_FLIP, FX_MATRIX, FX_RANDOM };
enum { H_STILL, H_BLINK, H_BREATHE, H_WAVE, H_BOUNCE, H_SHAKE, H_SHINE, H_SPARKLE, H_FLICKER, H_JELLY, H_GLITCH };
enum { C_SOLID, C_RAINBOW, C_GRAD_H, C_GRAD_V, C_RAINBOW_V, C_FIRE, C_NEON, C_CANDY, C_PIECES };
enum { D_SHADOW = 1, D_OUTLINE = 2, D_GLOW = 4, D_RAILS = 8 };
enum { PH_IN, PH_HOLD, PH_OUT, PH_GAP };

typedef struct { uint8_t kind, h, scale, rsv; uint16_t w, off, len; uint8_t st[TEXTFX_STYLE]; } tmsg_t;
typedef struct { uint8_t magic, count; uint16_t used; tmsg_t m[TEXTFX_MSGS]; uint8_t pool[TEXTFX_POOL]; } tlist_t;
_Static_assert(sizeof(tmsg_t) == 26, "message header layout");
_Static_assert(sizeof(tlist_t) == TEXTFX_SERIAL_MAX, "playlist layout");
#define MAGIC 0xA7u

static tlist_t L;

#define FCOLS 640
static uint16_t fcols[FCOLS];                 /* panel-font message, rendered with its tokens */
static int fw, fh;
static uint32_t fnext;

static fb_t bgfb;                             /* background animation keeps its own frame */

static struct {
    uint8_t cur, phase, rep, fx_in, fx_out, live, marquee;
    int npieces, w, h, x0, y0, win;
    uint32_t t0, dur;
} R;

static int iabs(int v) { return v < 0 ? -v : v; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static uint32_t hsh(uint32_t a, uint32_t b)
{
    uint32_t h = a * 2654435761u ^ (b + 0x9E3779B9u) * 2246822519u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    return h;
}

static rgb_t mix(rgb_t a, rgb_t b, int f, int n)
{
    if (n <= 0) return a;
    f = clampi(f, 0, n);
    int ar = (int)(a >> 16) & 255, ag = (int)(a >> 8) & 255, ab = (int)a & 255;
    int br = (int)(b >> 16) & 255, bg = (int)(b >> 8) & 255, bb = (int)b & 255;
    return RGB((ar * (n - f) + br * f) / n, (ag * (n - f) + bg * f) / n, (ab * (n - f) + bb * f) / n);
}

/* ---- playlist ------------------------------------------------------------------------------------ */
void textfx_reset(void) { L.magic = MAGIC; L.count = 0; L.used = 0; R.live = 0; }
int  textfx_count(void) { return L.count; }
void textfx_start(uint32_t now) { (void)now; R.live = 0; }

const uint8_t *textfx_serial(unsigned *len)
{
    if (!L.count) return 0;
    *len = 108u + L.used;
    return (const uint8_t *)&L;
}

static unsigned strip_bytes(unsigned h, unsigned w) { return w * ((h + 7u) / 8u); }

static int msg_ok(const tmsg_t *m, unsigned used)
{
    if (m->off + m->len > used) return 0;
    if (m->kind == K_STRIP) return m->h >= 1 && m->h <= 24 && m->w >= 1 && m->w <= 1024 && m->len == strip_bytes(m->h, m->w);
    if (m->kind == K_FONT)  return m->len >= 1 && m->len <= 120;
    return 0;
}

static void default_style(uint8_t *st, rgb_t c, unsigned speed, unsigned flags)
{
    for (unsigned i = 0; i < TEXTFX_STYLE; i++) st[i] = 0;
    st[0] = (flags & 2u) ? FX_SCROLL_R : FX_SCROLL_L;
    st[3] = (flags & 1u) ? C_RAINBOW : C_SOLID;
    st[4] = (uint8_t)(c >> 16); st[5] = (uint8_t)(c >> 8); st[6] = (uint8_t)c;
    st[7] = st[8] = st[9] = 255;
    st[10] = (uint8_t)(speed < 1 ? 1 : speed > 10 ? 10 : speed);
    st[12] = (uint8_t)(((flags & 2u) ? 4u : 0u) | (D_RAILS << 4));
}

static int add_strip(const uint8_t *st, unsigned h, unsigned w, const uint8_t *data)
{
    unsigned n = strip_bytes(h, w);
    if (L.count >= TEXTFX_MSGS || h < 1 || h > 24 || w < 1 || w > 1024 || L.used + n > TEXTFX_POOL) return 0;
    tmsg_t *m = &L.m[L.count];
    m->kind = K_STRIP; m->h = (uint8_t)h; m->scale = 0; m->rsv = 0; m->w = (uint16_t)w;
    m->off = L.used; m->len = (uint16_t)n;
    for (unsigned i = 0; i < TEXTFX_STYLE; i++) m->st[i] = st[i];
    for (unsigned i = 0; i < n; i++) L.pool[L.used + i] = data[i];
    L.used = (uint16_t)(L.used + n);
    L.count++;
    return 1;
}

int textfx_add(const uint8_t *p, unsigned len)
{
    if (len < 24) return 0;
    unsigned idx = p[4], count = p[5], kind = p[6];
    const uint8_t *st = p + 7;
    if (count < 1 || count > TEXTFX_MSGS || idx >= count) return 0;
    if (idx == 0) textfx_reset();
    if (idx != L.count) return 0;                        /* in order only */
    if (kind == K_STRIP) {
        if (len < 27) return 0;
        unsigned h = p[23], w = p[24] | ((unsigned)p[25] << 8);
        if (len != 26u + strip_bytes(h, w) || !add_strip(st, h, w, p + 26)) return 0;
    } else if (kind == K_FONT) {
        unsigned n = len - 24u;
        if (n < 1 || n > 120 || L.used + n > TEXTFX_POOL) return 0;
        tmsg_t *m = &L.m[L.count];
        m->kind = K_FONT; m->h = 0; m->scale = p[23] ? 1 : 0; m->rsv = 0; m->w = 0;
        m->off = L.used; m->len = (uint16_t)n;
        for (unsigned i = 0; i < TEXTFX_STYLE; i++) m->st[i] = st[i];
        for (unsigned i = 0; i < n; i++) L.pool[L.used + i] = p[24 + i];
        L.used = (uint16_t)(L.used + n);
        L.count++;
    } else {
        return 0;
    }
    R.live = 0;
    return idx + 1 == count ? 2 : 1;
}

/* 99.14 single strip: r g b speed flags h w_lo w_hi columns */
int textfx_legacy_strip(const uint8_t *p, unsigned len)
{
    if (len < 13) return 0;
    unsigned h = p[9], w = p[10] | ((unsigned)p[11] << 8);
    if (h < 1 || h > 24 || len != 12u + strip_bytes(h, w)) return 0;
    uint8_t st[TEXTFX_STYLE];
    default_style(st, RGB(p[4], p[5], p[6]), p[7], p[8]);
    textfx_reset();
    return add_strip(st, h, w, p + 12);
}

int textfx_load(const uint8_t *p, unsigned len)
{
    if (len >= 108 && p[0] == MAGIC) {                   /* saved playlist */
        const tlist_t *s = (const tlist_t *)(const void *)p;
        if (s->count < 1 || s->count > TEXTFX_MSGS || s->used > TEXTFX_POOL || len != 108u + s->used) return 0;
        for (unsigned i = 0; i < s->count; i++) if (!msg_ok(&s->m[i], s->used)) return 0;
        const uint8_t *src = p;
        uint8_t *dst = (uint8_t *)&L;
        for (unsigned i = 0; i < len; i++) dst[i] = src[i];
        R.live = 0;
        return 1;
    }
    if (len >= 4) {                                      /* 99.14: h | rtl << 7, w, columns */
        unsigned h = p[0] & 31u, w = p[1] | ((unsigned)p[2] << 8);
        if (h < 1 || h > 24 || w < 1 || w > 1024 || len != 3u + strip_bytes(h, w)) return 0;
        uint8_t st[TEXTFX_STYLE];
        default_style(st, RGB(255, 255, 255), 5, 1u | ((p[0] & 0x80u) ? 2u : 0u));
        textfx_reset();
        return add_strip(st, h, w, p + 3);
    }
    return 0;
}

/* ---- panel-font messages with tokens ---------------------------------------------------------------- */
static const char *const DOW3[7] = { "MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN" };

static int tok(const char *s, unsigned len, unsigned i, const char *name)   /* "{name}" at i -> its length */
{
    if (s[i] != '{') return 0;
    unsigned k = 0;
    while (name[k]) { if (i + 1 + k >= len || s[i + 1 + k] != name[k]) return 0; k++; }
    return (i + 1 + k < len && s[i + 1 + k] == '}') ? (int)k + 2 : 0;
}

static int put2(char *o, int n, unsigned v) { o[n++] = (char)('0' + v / 10u % 10u); o[n++] = (char)('0' + v % 10u); return n; }

static void font_build(const tmsg_t *m, uint32_t now)
{
    char o[104];
    int n = 0, k;
    const char *s = (const char *)&L.pool[m->off];
    clock_tm_t tm;
    clock_now(now, &tm);
    for (unsigned i = 0; i < m->len && n < 92; ) {
        if ((k = tok(s, m->len, i, "time"))) { n = put2(o, n, tm.h); o[n++] = ':'; n = put2(o, n, tm.m); i += (unsigned)k; continue; }
        if ((k = tok(s, m->len, i, "secs"))) { n = put2(o, n, tm.h); o[n++] = ':'; n = put2(o, n, tm.m); o[n++] = ':'; n = put2(o, n, tm.s); i += (unsigned)k; continue; }
        if ((k = tok(s, m->len, i, "date"))) { n = put2(o, n, tm.day); o[n++] = '.'; n = put2(o, n, tm.mon); i += (unsigned)k; continue; }
        if ((k = tok(s, m->len, i, "day")))  { const char *d = DOW3[(tm.dow + 6u) % 7u]; o[n++] = d[0]; o[n++] = d[1]; o[n++] = d[2]; i += (unsigned)k; continue; }
        if ((k = tok(s, m->len, i, "year"))) { n = put2(o, n, tm.year / 100u); n = put2(o, n, tm.year % 100u); i += (unsigned)k; continue; }
        o[n++] = s[i++];
    }
    int sc = m->scale ? 2 : 1;
    fw = 0; fh = 7 * sc;
    for (int i = 0; i < n && fw < FCOLS - 14; i++) {
        char ch = o[i];
        if (ch < 32 || ch > 126) ch = '?';
        if (ch == ' ') { for (int j = 0; j < 3 * sc; j++) fcols[fw++] = 0; continue; }
        const uint8_t *g = font5x7[ch - 32];
        int a = 0, b = 4;
        while (a < 4 && !g[a]) a++;
        while (b > a && !g[b]) b--;
        for (int col = a; col <= b; col++) {
            uint16_t v = g[col] & 0x7Fu;
            if (sc == 2) { uint16_t d = 0; for (int r = 0; r < 7; r++) if ((v >> r) & 1u) d |= (uint16_t)(3u << (2 * r)); v = d; }
            for (int j = 0; j < sc; j++) fcols[fw++] = v;
        }
        for (int j = 0; j < sc; j++) fcols[fw++] = 0;
    }
    while (fw > 1 && !fcols[fw - 1]) fw--;
    if (fw < 1) fw = 1;
}

/* ---- timeline ------------------------------------------------------------------------------------- */
static uint32_t colbits(const tmsg_t *m, int c)
{
    if (c < 0 || c >= R.w) return 0;
    if (m->kind == K_FONT) return fcols[c];
    unsigned bpc = (m->h + 7u) / 8u;
    const uint8_t *p = &L.pool[m->off + (unsigned)c * bpc];
    uint32_t v = p[0];
    if (bpc > 1) v |= (uint32_t)p[1] << 8;
    if (bpc > 2) v |= (uint32_t)p[2] << 16;
    return v;
}

static int speed_of(const uint8_t *st) { return st[10] < 1 ? 5 : st[10] > 10 ? 10 : st[10]; }
static int pps_of(const uint8_t *st)   { return 6 + speed_of(st) * 4; }
static uint32_t fx_ms(int fx, const uint8_t *st) { return fx == FX_NONE ? 0u : 1700u - (uint32_t)speed_of(st) * 130u; }

static void geometry(const tmsg_t *m)
{
    const uint8_t *st = m->st;
    R.win = R.w < 32 ? R.w : 32;
    int al = st[12] & 3;
    R.y0 = al == 1 ? 1 : al == 2 ? 31 - R.h : (32 - R.h) / 2;
    if (R.y0 < 0) R.y0 = 0;
    R.x0 = R.w < 32 ? (32 - R.w) / 2 : 0;
    R.marquee = (uint8_t)(R.w > 32 && (R.fx_in == FX_SCROLL_L || R.fx_in == FX_SCROLL_R));
    int np = 0, gap = 1;
    for (int c = 0; c < R.w; c++) { if (colbits(m, c)) { if (gap) np++; gap = 0; } else gap = 1; }
    R.npieces = np ? np : 1;
}

static uint32_t hold_ms(const uint8_t *st)
{
    uint32_t pause = st[11] ? (uint32_t)st[11] * 250u : 2000u, pps = (uint32_t)pps_of(st);
    if (R.marquee) return (uint32_t)(R.w + 32) * 1000u / pps;
    if (R.w > 32)  return 700u + (uint32_t)(R.w - 32) * 1000u / pps + pause;
    return pause;
}

static int pick(uint8_t fx) { return fx >= FX_RANDOM ? 1 + (int)gfx_rnd(FX_RANDOM - 1) : fx; }

static void phase_start(int ph, uint32_t now)
{
    const uint8_t *st = L.m[R.cur].st;
    R.phase = (uint8_t)ph; R.t0 = now;
    R.dur = ph == PH_IN ? fx_ms(R.fx_in, st) : ph == PH_OUT ? fx_ms(R.fx_out, st) : ph == PH_HOLD ? hold_ms(st) : 250u;
}

static void start_msg(unsigned i, uint32_t now, int first)
{
    const tmsg_t *m = &L.m[i];
    R.cur = (uint8_t)i; R.live = 1;
    if (first) R.rep = 0;
    R.fx_in = (uint8_t)pick(m->st[0]); R.fx_out = (uint8_t)pick(m->st[2]);
    if (m->kind == K_FONT) { font_build(m, now); R.w = fw; R.h = fh; fnext = now + 500; }
    else { R.w = m->w; R.h = m->h; }
    geometry(m);
    if (first && m->st[13] >= 2 && (unsigned)(m->st[13] - 2) < effect_count) effects[m->st[13] - 2].init();
    phase_start(R.marquee ? PH_HOLD : PH_IN, now);
}

static void advance(uint32_t now)
{
    for (int guard = 0; guard < 8 && now - R.t0 >= R.dur; guard++) {
        uint32_t end = R.t0 + R.dur;
        switch (R.phase) {
        case PH_IN:   phase_start(PH_HOLD, end); break;
        case PH_HOLD: phase_start(R.marquee ? PH_GAP : PH_OUT, end); break;
        case PH_OUT:  phase_start(PH_GAP, end); break;
        default: {
            unsigned rep = L.m[R.cur].st[15] ? L.m[R.cur].st[15] : 1;
            if (++R.rep < rep) start_msg(R.cur, end, 0);
            else start_msg((R.cur + 1u) % L.count, end, 1);
            break; }
        }
    }
}

/* ---- effects ---------------------------------------------------------------------------------------- */
/* entry (out = 0) or exit (out = 1) at progress p 0..1000; k = column inside the shown window */
static int apply_fx(int fx, int out, int p, int k, int c, int r, int piece, int rtl, int *x, int *y, int *a, int *green)
{
    int W = R.win, H = R.h, q = out ? 1000 - p : p;
    uint32_t hh = hsh((uint32_t)c, (uint32_t)r);
    switch (fx) {
    case FX_SCROLL_L: *x += out ? -p * (R.x0 + W) / 1000 : (1000 - p) * (32 - R.x0) / 1000; return 1;
    case FX_SCROLL_R: *x += out ? p * (32 - R.x0) / 1000 : -(1000 - p) * (R.x0 + W) / 1000; return 1;
    case FX_SCROLL_U: *y += out ? -p * (R.y0 + H) / 1000 : (1000 - p) * (32 - R.y0) / 1000; return 1;
    case FX_SCROLL_D: *y += out ? p * (32 - R.y0) / 1000 : -(1000 - p) * (R.y0 + H) / 1000; return 1;
    case FX_TYPE: case FX_WIPE: case FX_LASER: { int kk = rtl ? W - 1 - k : k; return kk * 1000 < q * W; }
    case FX_OPEN:   return iabs(2 * k - (W - 1)) * 1000 < q * W;
    case FX_CLOSE: { int d = k < W - 1 - k ? k : W - 1 - k; return 2 * d * 1000 < q * W; }
    case FX_BLINDS: return (k % 4) * 1000 < q * 4;
    case FX_DISSOLVE: return (int)(hh % 1000u) < q;
    case FX_MESH:
        if (k & 1) *y += (out ? p : 1000 - p) * (32 - R.y0) / 1000;
        else       *y -= (out ? p : 1000 - p) * (R.y0 + H) / 1000;
        return 1;
    case FX_SLICE: {
        int lp = clampi((p * (W + 6) / 1000 - k) * 1000 / 6, 0, 1000);
        if (!out) *x += (1000 - lp) * (32 - *x) / 1000; else *x -= lp * (*x + 1) / 1000;
        return 1; }
    case FX_DROP: {
        int lp = clampi((p * (R.npieces + 1) - piece * 1000) / 2, 0, 1000);
        if (!out) { int e = lp < 700 ? lp * lp / 490 : 1000 - (lp - 700) * (1000 - lp) / 190; *y -= (1000 - e) * (R.y0 + H + 2) / 1000; }
        else *y += lp * lp / 1000 * (33 - R.y0) / 1000;
        return 1; }
    case FX_GROW_U: return (H - 1 - r) * 1000 < q * H;
    case FX_GROW_D: return r * 1000 < q * H;
    case FX_ZOOM: {
        if (q < 40) return 0;
        int cx = R.x0 + W / 2, cy = R.y0 + H / 2;
        *x = cx + (*x - cx) * q / 1000; *y = cy + (*y - cy) * q / 1000;
        return 1; }
    case FX_FADE: *a = *a * q / 1000; return 1;
    case FX_RAIN: case FX_MATRIX: {
        int lp = clampi(p * 3 / 2 - (int)(hh % 500u), 0, 1000);
        if (!out) {
            if (!lp) return 0;
            *y -= (1000 - lp) * (*y + 1) / 1000;
            if (fx == FX_MATRIX && lp < 1000) *green = 1;
            return 1;
        }
        if (fx == FX_MATRIX && lp) *green = 1;
        *y += lp * lp / 1000 * (33 - *y) / 1000;
        return *y < 32; }
    case FX_EXPLODE: {
        int ox = (int)(hh % 48u) - 8, oy = (int)((hh >> 8) % 48u) - 8;
        if (!out) {
            if (!p) return 0;
            int e = 1000 - (1000 - p) * (1000 - p) / 1000;
            *x = ox + (*x - ox) * e / 1000; *y = oy + (*y - oy) * e / 1000;
        } else {
            *x += ((*x - 15) * 3 + ox - 16) * p / 1000; *y += ((*y - 15) * 3 + oy - 16) * p / 1000;
            *a = *a * (1000 - p) / 1000;
        }
        return 1; }
    case FX_FLIP: {
        if (q < 60) return 0;
        int cy = R.y0 + H / 2;
        *y = cy + (*y - cy) * q / 1000;
        return 1; }
    default: return 1;
    }
}

/* while shown (motion effects also during entry/exit) */
static void apply_hold(int hold, int holding, uint32_t now, int c, int r, int piece, int *x, int *y, int *a, int *white)
{
    switch (hold) {
    case H_BLINK:   if (holding && ((now / 400) & 1)) *a = 0; break;
    case H_BREATHE: if (holding) *a = *a * (60 + sin8[(uint8_t)(now / 6)] * 195 / 255) / 255; break;
    case H_WAVE:    *y += ((int)sin8[(uint8_t)(c * 14 + now / 4)] - 128) * 2 / 127; break;
    case H_BOUNCE:  *y -= ((int)sin8[(uint8_t)((now / 4) & 127)] - 128) * 3 / 127; break;
    case H_SHAKE: { uint32_t h = hsh(now / 60, 7); *x += (int)(h & 1) - (int)((h >> 1) & 1); *y += (int)((h >> 2) & 1) - (int)((h >> 3) & 1); break; }
    case H_SHINE: { int pos = (int)((now / 25) % 80u) - 20, b = *x + *y - pos; if (b >= 0 && b < 3) *white = 200; break; }
    case H_SPARKLE: if (hsh((uint32_t)(c * 31 + r), now / 90) % 17u == 0) *white = 255; break;
    case H_FLICKER: if (holding && hsh(now / 70, 3) % 9u == 0) *a = *a / 4; break;
    case H_JELLY:   *y += ((int)sin8[(uint8_t)(piece * 50 + now / 3)] - 128) * 2 / 127; break;
    case H_GLITCH:  if (now % 1700u < 160u) *x += (int)(hsh((uint32_t)r, now / 40) % 5u) - 2; break;
    default: break;
    }
}

static rgb_t fire(int heat)
{
    heat = clampi(heat, 0, 255);
    if (heat < 85)  return RGB(heat * 3, 0, 0);
    if (heat < 170) return RGB(255, (heat - 85) * 3, 0);
    return RGB(255, 255, (heat - 170) * 3);
}

static rgb_t text_color(const uint8_t *st, int c, int r, int x, int y, int piece, uint32_t now)
{
    rgb_t c1 = RGB(st[4], st[5], st[6]), c2 = RGB(st[7], st[8], st[9]);
    switch (st[3]) {
    case C_RAINBOW:   return gfx_hsv((uint8_t)(c * 4 - (int)(now / 12)), 230, 255);
    case C_GRAD_H:    return mix(c1, c2, c, R.w - 1);
    case C_GRAD_V:    return mix(c1, c2, r, R.h - 1);
    case C_RAINBOW_V: return gfx_hsv((uint8_t)(r * 255 / (R.h + 1) + (int)(now / 10)), 230, 255);
    case C_FIRE:      return fire(80 + r * 175 / (R.h > 1 ? R.h - 1 : 1) - (int)(hsh((uint32_t)(c * 64 + r), now / 70) % 110u));
    case C_NEON:      return gfx_scale(c1, (uint8_t)(hsh(now / 90, 5) % 13u == 0 ? 140 : 255));
    case C_CANDY:     return (((x + y + (int)(now / 70)) / 3) & 1) ? c1 : c2;
    case C_PIECES:    return gfx_hsv((uint8_t)(hsh((uint32_t)piece, 99) & 255u), 200, 255);
    default:          return c1;
    }
}

static void background(const uint8_t *st, uint32_t now)
{
    unsigned bg = st[13], dim = st[14] ? st[14] : 90;
    fb_t *f = gfx_fb();
    if (bg >= 2 && bg - 2u < effect_count) {
        gfx_target(&bgfb);
        effects[bg - 2].frame(now);
        gfx_target(f);
        const uint8_t *s = &bgfb[0][0][0];
        uint8_t *d = &(*f)[0][0][0];
        for (unsigned i = 0; i < sizeof bgfb; i++) d[i] = (uint8_t)(s[i] * dim / 256u);
    } else if (bg == 1) {
        gfx_fill(gfx_scale(RGB(st[7], st[8], st[9]), (uint8_t)(dim > 255 ? 255 : dim)));
    } else {
        gfx_fill(0);
    }
}

/* ---- drawing -------------------------------------------------------------------------------------- */
void textfx_render(uint32_t now)
{
    if (!L.count) { gfx_fill(0); return; }
    if (!R.live || R.cur >= L.count) start_msg(0, now, 1);
    advance(now);
    const tmsg_t *m = &L.m[R.cur];
    const uint8_t *st = m->st;
    if (m->kind == K_FONT && (int32_t)(now - fnext) >= 0) {     /* tokens change: rebuild */
        int ow = fw;
        fnext = now + 500;
        font_build(m, now);
        R.w = fw; R.h = fh;
        if (fw != ow) geometry(m);
    }
    background(st, now);
    int decor = st[12] >> 4, rtl = (st[12] >> 2) & 1, hold = st[1];
    if (decor & D_RAILS)
        for (int i = 0; i < 32; i++) {
            if (R.y0 - 3 >= 0) gfx_px(i, R.y0 - 3, gfx_hsv((uint8_t)(i * 8 + now / 10), 255, 60));
            if (R.y0 + R.h + 2 < 32) gfx_px(i, R.y0 + R.h + 2, gfx_hsv((uint8_t)(i * 8 - now / 10), 255, 60));
        }
    if (R.phase == PH_GAP) return;

    uint32_t t = now - R.t0;
    int p = R.dur ? clampi((int)(t * 1000u / R.dur), 0, 1000) : 1000;
    int pps = pps_of(st), x0 = R.x0, view = 0, c_lo, c_hi;
    if (R.marquee) {
        int travel = (int)(t * (uint32_t)pps / 1000u);
        x0 = R.fx_in == FX_SCROLL_L ? 32 - travel : -R.w + travel;
        c_lo = clampi(-x0 - 3, 0, R.w); c_hi = clampi(35 - x0, 0, R.w);
    } else {
        if (R.w > 32) {
            int v = 0;
            if (R.phase == PH_HOLD) v = t > 700 ? (int)((t - 700) * (uint32_t)pps / 1000u) : 0;
            else if (R.phase == PH_OUT) v = R.w - 32;
            v = clampi(v, 0, R.w - 32);
            view = rtl ? (R.w - 32) - v : v;
        }
        c_lo = view; c_hi = view + R.win;
    }
    int fx = R.marquee ? FX_NONE : R.phase == PH_IN ? R.fx_in : R.phase == PH_OUT ? R.fx_out : FX_NONE, out = R.phase == PH_OUT;
    int holding = R.phase == PH_HOLD;

    int piece0 = 0, gap = 1;                                 /* pieces = groups of columns (letters / words) */
    for (int c = 0; c < c_lo; c++) { if (colbits(m, c)) { if (gap) piece0++; gap = 0; } else gap = 1; }
    int gap0 = gap;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 0 && !(decor & (D_SHADOW | D_OUTLINE | D_GLOW))) continue;
        int piece = piece0;
        gap = gap0;
        for (int c = c_lo; c < c_hi; c++) {
            uint32_t bits = colbits(m, c);
            if (!bits) { gap = 1; continue; }
            if (gap) { piece++; gap = 0; }
            int k = c - view;
            for (int r = 0; r < R.h; r++) {
                if (!((bits >> r) & 1u)) continue;
                int x = x0 + (c - view), y = R.y0 + r, a = 255, green = 0, white = 0;
                if (fx != FX_NONE && !apply_fx(fx, out, p, k, c, r, piece - 1, rtl, &x, &y, &a, &green)) continue;
                apply_hold(hold, holding, now, c, r, piece - 1, &x, &y, &a, &white);
                if (a <= 0) continue;
                rgb_t col = green ? RGB(40, 255, 90) : text_color(st, c, r, x, y, piece - 1, now);
                if (white) col = mix(col, RGB(255, 255, 255), white, 255);
                if (pass == 0) {
                    if (decor & D_GLOW)
                        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) gfx_px_add(x + dx, y + dy, gfx_scale(col, 45));
                    if (decor & D_OUTLINE)
                        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) gfx_px(x + dx, y + dy, RGB(0, 0, 0));
                    if (decor & D_SHADOW) gfx_px(x + 1, y + 1, RGB(0, 0, 0));
                } else {
                    gfx_px(x, y, a >= 255 ? col : gfx_scale(col, (uint8_t)a));
                }
            }
        }
    }

    if ((fx == FX_TYPE || fx == FX_WIPE || fx == FX_LASER) && p < 1000) {   /* cursor / light bar / laser */
        int q = out ? 1000 - p : p, kk = q * R.win / 1000;
        int ex = x0 + (rtl ? R.win - 1 - kk : kk);
        if (fx == FX_TYPE) { if ((now / 150) & 1) gfx_rect(ex, R.y0, 1, R.h, text_color(st, kk + view, 0, ex, R.y0, 0, now), 1); }
        else if (fx == FX_WIPE) gfx_rect(ex, R.y0 - 1, 2, R.h + 2, RGB(255, 255, 255), 1);
        else {
            int ty = R.y0 + (int)(hsh(now / 40, 11) % (uint32_t)R.h);
            gfx_line(rtl ? 0 : 31, 0, ex, ty, RGB(255, 40, 40));
            gfx_px(ex, ty, RGB(255, 255, 255));
        }
    }
}
