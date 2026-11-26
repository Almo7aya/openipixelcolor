/* Mode manager: what is on the panel, the key, and the app-level BLE commands.
 * Hardware independent (uses app_port.h), so it runs unchanged in the PC simulator. */
#include "modes.h"
#include "gfx.h"
#include "effects.h"
#include "clockface.h"
#include "app_port.h"
#include "fwinfo.h"
#include "anim.h"
#include "screens.h"

#define TEXT_MAX MODES_TEXT_MAX

static struct {
    uint8_t  mode, effect, autoplay, power, clock_style, fmt24, show_date, test_pattern, orient;
    uint8_t  pmode;                                   /* last lasting mode, shown again after a power cycle */
    uint8_t  text_kind;                               /* 0 built-in font, 1 message playlist (textfx.c) */
    uint16_t autoplay_s;
    uint32_t mode_since, effect_since;
    uint8_t  bright_pct;
    char     text[TEXT_MAX + 1];
    rgb_t    text_color;
    uint8_t  text_rainbow, text_speed;
    int32_t  text_x_q8;
    uint32_t text_last;
    uint8_t  lvl[11], rstyle, ranimated;
    uint8_t  s1, s2;
    uint8_t  tstate;
    uint32_t tstart, tacc;
    uint8_t  next_after_splash;
    uint8_t  anim_slot, anim_speed;
    uint8_t  first_start;                             /* nothing saved yet (settings.c): welcome after the boot screen */
} S;

static fb_t canvas;
/* text_kind 1: the message playlist of textfx.c (strips drawn by the app, panel-font text with tokens) */

static const char *const DEFAULT_TEXT = "openipixelcolor " FW_VERSION_STR " - hello!";

/* ------------------------------------------------------------------------------------------------ */
static void start_effect(unsigned e, uint32_t now)
{
    S.effect = (uint8_t)(e % effect_count);
    S.effect_since = now;
    effects[S.effect].init();
}

/* modes worth coming back to after a power cycle (not the timer, test patterns or the splash) */
static int lasting(unsigned m)
{
    return m == MODE_EFFECT || m == MODE_CLOCK || m == MODE_TEXT || m == MODE_CANVAS ||
           m == MODE_RHYTHM || m == MODE_SCORE || m == MODE_ANIM;
}

void modes_set(unsigned mode, unsigned arg, uint32_t now)
{
    if (mode >= MODE_COUNT) return;
    S.mode = (uint8_t)mode;
    S.mode_since = now;
    if (lasting(mode)) S.pmode = (uint8_t)mode;
    switch (mode) {
    case MODE_EFFECT: start_effect(arg, now); break;
    case MODE_CLOCK:  S.clock_style = (uint8_t)(arg % CLOCK_STYLES); break;
    case MODE_TEXT:
        S.text_x_q8 = 32 << 8;
        if (S.text_kind) textfx_start(now);
        S.text_last = now;
        break;
    case MODE_TEST:   S.test_pattern = (uint8_t)(arg % 7); break;
    case MODE_ANIM:
        S.anim_slot = (uint8_t)(arg < ANIM_SLOTS ? arg : ANIM_ALL);
        anim_start(S.anim_slot, now);
        break;
    default: break;
    }
}

void modes_set_time(uint8_t h, uint8_t m, uint8_t s, uint32_t now) { clock_set_time(h, m, s, now); }

static void copy_text(const char *s, unsigned len)   /* printable ASCII only, always terminated */
{
    if (len > TEXT_MAX) len = TEXT_MAX;
    unsigned j = 0;
    for (unsigned i = 0; i < len && s[i]; i++) {
        char c = s[i];
        if (c >= 32 && c <= 126) S.text[j++] = c;
    }
    S.text[j] = 0;
}

static void set_text_style(uint32_t rgb, unsigned speed, unsigned flags)
{
    S.text_color = rgb ? rgb : RGB(255, 255, 255);
    S.text_speed = (uint8_t)(speed < 1 ? 1 : speed > 10 ? 10 : speed);
    S.text_rainbow = (uint8_t)(flags & 1u);
}

void modes_set_text(const char *s, unsigned len, uint32_t rgb, unsigned speed, unsigned flags, uint32_t now)
{
    copy_text(s, len);
    set_text_style(rgb, speed, flags);
    S.text_kind = 0;
    modes_set(MODE_TEXT, 0, now);
}

const uint8_t *modes_textbits(unsigned *len)
{
    if (!S.text_kind) return 0;
    return textfx_serial(len);
}

int modes_restore_textbits(const uint8_t *p, unsigned len) { return textfx_load(p, len); }

static void clear_canvas(void)
{
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) canvas[y][x][0] = canvas[y][x][1] = canvas[y][x][2] = 0;
}

static void set_defaults(void)
{
    S.power = 1; S.autoplay = 1; S.autoplay_s = 20; S.fmt24 = 1; S.show_date = 1; S.bright_pct = 80;
    S.orient = 2;                                      /* matches display.c default */
    S.effect = 0; S.clock_style = 0;
    S.rstyle = 0; S.ranimated = 0; S.s1 = 0; S.s2 = 0;
    for (int i = 0; i < 11; i++) S.lvl[i] = 0;
    S.tstate = 0; S.tacc = 0;
    copy_text(DEFAULT_TEXT, TEXT_MAX);
    set_text_style(RGB(255, 200, 80), 4, 1);
    S.text_kind = 0; textfx_reset();
    clear_canvas();
    S.anim_slot = ANIM_ALL; S.anim_speed = 10;     /* the stored animations themselves are not settings: kept */
    S.pmode = MODE_EFFECT;
    app_set_brightness_pct(S.bright_pct);
    app_set_orientation(S.orient);
}

void modes_init(uint32_t now)
{
    set_defaults();
    anim_init();                                       /* before settings_load(): a saved mode may be an animation */
    modes_set(MODE_SPLASH, 0, now);
}

void modes_defaults(uint32_t now)
{
    set_defaults();
    modes_set(MODE_EFFECT, 0, now);
}

/* ---- settings that survive a power cycle --------------------------------------------------- */
void modes_snapshot(modes_persist_t *p)
{
    uint8_t *b = (uint8_t *)p;
    for (unsigned i = 0; i < sizeof *p; i++) b[i] = 0;    /* unused bytes stay 0: compared and CRC'd */
    p->ver = MODES_PERSIST_VER;
    p->mode = S.pmode;
    p->effect = S.autoplay ? 0 : S.effect;    /* autoplay moves on every few s: not a setting, don't wear the flash */
    p->autoplay = S.autoplay;
    p->autoplay_lo = (uint8_t)S.autoplay_s; p->autoplay_hi = (uint8_t)(S.autoplay_s >> 8);
    p->bright_pct = S.bright_pct; p->orient = S.orient;
    p->clock_style = S.clock_style; p->fmt24 = S.fmt24; p->show_date = S.show_date;
    p->text_r = (uint8_t)(S.text_color >> 16); p->text_g = (uint8_t)(S.text_color >> 8); p->text_b = (uint8_t)S.text_color;
    p->text_rainbow = S.text_rainbow; p->text_speed = S.text_speed;
    p->rstyle = S.rstyle; p->ranimated = S.ranimated;
    for (int i = 0; i < 11; i++) p->lvl[i] = S.lvl[i];
    p->s1 = S.s1; p->s2 = S.s2;
    for (unsigned i = 0; i < TEXT_MAX && S.text[i]; i++) p->text[i] = S.text[i];
    p->text_kind = S.text_kind;
    p->anim_slot = S.anim_slot; p->anim_speed = S.anim_speed;
}

/* Applies saved settings (call right after modes_init: the splash then continues into the saved mode).
 * Everything is clamped, so a damaged record can only give odd settings, never a crash. */
void modes_restore(const modes_persist_t *p, uint32_t now)
{
    (void)now;
    if (p->ver < 1 || p->ver > MODES_PERSIST_VER) return;
    S.text_kind = (uint8_t)(p->ver >= 2 && p->text_kind == 1 && textfx_count() ? 1 : 0);   /* playlist restored first */
    S.pmode = (uint8_t)(lasting(p->mode) ? p->mode : MODE_EFFECT);
    S.effect = (uint8_t)(p->effect < effect_count ? p->effect : 0);
    S.autoplay = p->autoplay ? 1 : 0;
    unsigned ap = (unsigned)p->autoplay_lo | ((unsigned)p->autoplay_hi << 8);
    S.autoplay_s = (uint16_t)(ap < 3 ? 3 : ap);
    S.bright_pct = (uint8_t)(p->bright_pct > 100 ? 100 : p->bright_pct < 10 ? 10 : p->bright_pct);  /* never boot dark */
    S.orient = (uint8_t)(p->orient & 3u);
    S.clock_style = (uint8_t)(p->clock_style % CLOCK_STYLES);
    S.fmt24 = p->fmt24 ? 1 : 0; S.show_date = p->show_date ? 1 : 0;
    copy_text(p->text, TEXT_MAX);
    set_text_style(RGB(p->text_r, p->text_g, p->text_b), p->text_speed, p->text_rainbow);
    S.rstyle = p->rstyle; S.ranimated = p->ranimated ? 1 : 0;
    for (int i = 0; i < 11; i++) S.lvl[i] = (uint8_t)(p->lvl[i] & 15u);
    S.s1 = (uint8_t)(p->s1 % 100); S.s2 = (uint8_t)(p->s2 % 100);
    if (p->ver >= 3) {
        S.anim_slot = (uint8_t)(p->anim_slot < ANIM_SLOTS ? p->anim_slot : ANIM_ALL);
        S.anim_speed = (uint8_t)(p->anim_speed < 2 ? 10 : p->anim_speed > 40 ? 40 : p->anim_speed);
    }
    if (S.pmode == MODE_ANIM && !anim_valid(S.anim_slot)) S.pmode = MODE_EFFECT;   /* deleted or damaged since */
    app_set_brightness_pct(S.bright_pct);
    app_set_orientation(S.orient);
}

const uint8_t *modes_canvas(void) { return &canvas[0][0][0]; }

void modes_restore_canvas(const uint8_t *px)
{
    uint8_t *d = &canvas[0][0][0];
    for (unsigned i = 0; i < sizeof canvas; i++) d[i] = px[i];
}

void modes_first_start(int yes) { S.first_start = (uint8_t)(yes ? 1 : 0); }

static void enter_saved_mode(uint32_t now)
{
    if (S.first_start) { modes_set(MODE_WELCOME, 0, now); return; }
    unsigned arg = S.pmode == MODE_EFFECT ? S.effect : S.pmode == MODE_CLOCK ? S.clock_style :
                   S.pmode == MODE_ANIM ? S.anim_slot : 0;
    modes_set(S.pmode, arg, now);
}

/* ------------------------------------------------------------------------------------------------ */
static void draw_rails(int y1, int y2, uint32_t now)
{
    for (int i = 0; i < 32; i++) {                        /* thin animated rails */
        gfx_px(i, y1, gfx_hsv((uint8_t)(i * 8 + now / 10), 255, 50));
        gfx_px(i, y2, gfx_hsv((uint8_t)(i * 8 - now / 10), 255, 50));
    }
}

static void draw_text(uint32_t now)
{
    if (S.text_kind && textfx_count()) { textfx_render(now); return; }
    gfx_fill(0);
    uint32_t dt = now - S.text_last;
    S.text_last = now;
    S.text_x_q8 -= (int32_t)(dt * (6u + S.text_speed * 4u) * 256u / 1000u);
    int w = gfx_text_width(S.text);
    if ((S.text_x_q8 >> 8) < -w) S.text_x_q8 = 32 << 8;
    int x = S.text_x_q8 >> 8;
    for (int i = 0; S.text[i]; i++) {
        rgb_t c = S.text_rainbow ? gfx_hsv((uint8_t)(i * 18 + now / 12), 220, 255) : S.text_color;
        x += gfx_char(x, 12, S.text[i], c);
    }
    draw_rails(9, 21, now);
}

static void draw_score(void)
{
    gfx_fill(0);
    gfx_text(4, 1, "P1", RGB(255, 80, 80));
    gfx_text(20, 1, "P2", RGB(80, 140, 255));
    gfx_7seg(1, 11, 6, 11, 2, S.s1 / 10 ? S.s1 / 10 : 10, RGB(255, 60, 60));
    gfx_7seg(8, 11, 6, 11, 2, S.s1 % 10, RGB(255, 60, 60));
    gfx_7seg(18, 11, 6, 11, 2, S.s2 / 10 ? S.s2 / 10 : 10, RGB(60, 120, 255));
    gfx_7seg(25, 11, 6, 11, 2, S.s2 % 10, RGB(60, 120, 255));
    gfx_rect(15, 14, 2, 2, RGB(180, 180, 180), 1);
    gfx_rect(15, 18, 2, 2, RGB(180, 180, 180), 1);
}

static void draw_timer(uint32_t now)
{
    uint32_t ms = S.tacc + (S.tstate == 1 ? now - S.tstart : 0);
    uint32_t sec = ms / 1000u, mm = (sec / 60u) % 100u, ss = sec % 60u, tenth = (ms / 100u) % 10u;
    rgb_t c = S.tstate == 1 ? RGB(60, 255, 120) : S.tstate == 2 ? (((now / 400) & 1) ? RGB(255, 200, 40) : RGB(90, 70, 10))
                                                               : RGB(200, 200, 200);
    gfx_fill(0);
    static const int X[4] = { 1, 8, 18, 25 };
    int d[4] = { (int)(mm / 10), (int)(mm % 10), (int)(ss / 10), (int)(ss % 10) };
    for (int i = 0; i < 4; i++) gfx_7seg(X[i], 7, 6, 11, 2, d[i], c);
    gfx_rect(15, 9, 2, 2, c, 1); gfx_rect(15, 14, 2, 2, c, 1);
    gfx_rect(1, 22, (int)tenth * 3 + 3, 2, gfx_scale(c, 120), 1);
}

static void draw_test(uint32_t t)
{
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            uint8_t r = 0, g = 0, b = 0;
            switch (S.test_pattern) {
            case 0:
                if (y == 0) r = 255;
                if (x == 0) g = 255;
                if (x < 4 && y < 4) { r = 0; g = 0; b = 255; }
                if (x == y) r = g = b = 255;
                break;
            case 1: r = (x / 4) & 1 ? 255 : 0; g = (x / 4) & 2 ? 255 : 0; b = (x / 4) & 4 ? 255 : 0;
                    if (x / 4 == 0) r = g = b = 255;
                    break;
            case 2: if (y < 16) r = g = b = (uint8_t)(x * 8 + 7);
                    else if (y < 22) r = (uint8_t)(x * 8 + 7);
                    else if (y < 27) g = (uint8_t)(x * 8 + 7);
                    else b = (uint8_t)(x * 8 + 7);
                    break;
            case 3: r = g = b = ((x ^ y) & 1) ? 255 : 0; break;
            case 4: if (y == (int)((t / 100) & 31)) r = g = b = 255; break;
            case 5: if (x == (int)((t / 100) & 31)) r = g = b = 255; break;
            default: r = g = b = 255; break;                      /* full white (power check) */
            }
            gfx_px(x, y, RGB(r, g, b));
        }
}

static void draw_splash(uint32_t now)
{
    uint32_t t = now - S.mode_since;
    screens_boot(t > SCREENS_BOOT_MS ? SCREENS_BOOT_MS : t);
    if (t > SCREENS_BOOT_MS) enter_saved_mode(now);
}

/* an app connected (proto.c: the first packet after a quiet spell is its time sync): screens_connected()
 * over whatever plays, for a moment. Not over the boot screen. */
static struct { uint8_t show; uint32_t at; } conn;
void modes_connected(uint32_t now)
{
    if (S.mode == MODE_SPLASH) return;
    conn.show = 1;
    conn.at = now;
}

void modes_render(fb_t *fb, uint32_t now)
{
    gfx_target(fb);
    anim_tick(now);
    if (!S.power) { gfx_fill(0); return; }
    if (anim_draw_saving(now, S.mode == MODE_ANIM)) return;
    if (conn.show) {
        if (now - conn.at < SCREENS_CONNECTED_MS) { screens_connected(now - conn.at); return; }
        conn.show = 0;
    }
    switch (S.mode) {
    case MODE_EFFECT:
        if (S.autoplay && now - S.effect_since >= S.autoplay_s * 1000u) start_effect(S.effect + 1u, now);
        effects[S.effect].frame(now);
        break;
    case MODE_CLOCK:  clock_draw(S.clock_style, now, S.fmt24, S.show_date); break;
    case MODE_TEXT:   draw_text(now); break;
    case MODE_CANVAS: for (int y = 0; y < GFX_H; y++) for (int x = 0; x < GFX_W; x++)
                          for (int c = 0; c < 3; c++) (*fb)[y][x][c] = canvas[y][x][c];
                      break;
    case MODE_RHYTHM:
        if (S.ranimated) {
            uint8_t l[11];
            for (int i = 0; i < 11; i++) {
                uint32_t ph = now / (60u + S.lvl[0] * 15u) + (uint32_t)i * 37u;
                l[i] = (uint8_t)(((ph * 2654435761u) >> 28) & 15u);
            }
            eq_draw(l, S.rstyle, now);
        } else {
            eq_draw(S.lvl, S.rstyle, now);
        }
        break;
    case MODE_SCORE:  draw_score(); break;
    case MODE_TIMER:  draw_timer(now); break;
    case MODE_TEST:   draw_test(now); break;
    case MODE_ANIM:   anim_draw(now, S.anim_speed); break;
    case MODE_WELCOME: screens_welcome(now); break;
    default:          draw_splash(now); break;
    }
}

/* key: short = next effect / clock face / animation / back to effects, long = brightness step */
void modes_key(int ev, uint32_t now)
{
    if (ev == MKEY_LONG) {
        static const uint8_t STEP[5] = { 100, 60, 30, 10, 100 };
        unsigned i = 0;
        while (i < 4 && STEP[i] != S.bright_pct) i++;
        S.bright_pct = STEP[i + 1 > 4 ? 0 : i + 1];
        app_set_brightness_pct(S.bright_pct);
        return;
    }
    if (ev != MKEY_SHORT) return;
    if (!S.power) { S.power = 1; return; }
    switch (S.mode) {
    case MODE_EFFECT:
        S.autoplay = 0;
        if (S.effect + 1u >= effect_count) modes_set(MODE_CLOCK, 0, now);
        else start_effect(S.effect + 1u, now);
        break;
    case MODE_CLOCK:
        if (S.clock_style + 1u >= CLOCK_STYLES) modes_set(MODE_TEXT, 0, now);
        else S.clock_style++;
        break;
    case MODE_TEXT:
        if (anim_valid(ANIM_ALL)) { modes_set(MODE_ANIM, ANIM_ALL, now); break; }
        S.autoplay = 1;
        modes_set(MODE_EFFECT, 0, now);
        break;
    case MODE_ANIM:                                    /* next stored animation, after the last: effects */
        if (anim_next(now)) { if (S.anim_slot != ANIM_ALL) S.anim_slot = (uint8_t)anim_showing(); break; }
        S.autoplay = 1;
        modes_set(MODE_EFFECT, 0, now);
        break;
    default:
        modes_set(MODE_EFFECT, S.effect, now);
        break;
    }
}

/* ------------------------------------------------------------------------------------------------ */
static void reply_status(uint32_t now)
{
    clock_tm_t tm;
    clock_now(now, &tm);
    /* 16-byte reply: mode effect bright power hh mm ss flags orient clock_style autoplay_s
     * (animation mode: "effect" = the slot on screen, flags bit 4 = playing all, bit 5..7 unused) */
    uint8_t x[11] = { S.mode, S.mode == MODE_ANIM ? (uint8_t)anim_showing() : S.effect, S.bright_pct, S.power, tm.h, tm.m, tm.s,
                      (uint8_t)((tm.synced ? 1 : 0) | (S.autoplay ? 2 : 0) | (S.fmt24 ? 4 : 0) | (S.show_date ? 8 : 0) |
                                (S.anim_slot == ANIM_ALL ? 16 : 0)),
                      S.orient, S.clock_style, (uint8_t)(S.autoplay_s > 255 ? 255 : S.autoplay_s) };
    app_reply(0xF106, 1, x, sizeof x);
}

int modes_handle_cmd(uint16_t cmd, const uint8_t *p, unsigned len, uint32_t now)
{
    switch (cmd) {
    /* ---- commands of the vendor app (and pypixelcolor) ---- */
    case 0x8003:                                       /* clear */
        clear_canvas();
        S.autoplay = 1; modes_set(MODE_EFFECT, 0, now);
        return 1;
    case 0x8004:                                       /* brightness 0..100 */
        if (len < 5) return 0;
        S.bright_pct = (uint8_t)(p[4] > 100 ? 100 : p[4]);
        app_set_brightness_pct(S.bright_pct);
        return 1;
    case 0x8006:                                       /* orientation */
        if (len < 5) return 0;
        S.orient = (uint8_t)(p[4] & 3u);
        app_set_orientation(S.orient);
        return 1;
    case 0x8008:                                       /* show slot n -> effect n */
        if (len < 7) return 0;
        S.autoplay = 0; modes_set(MODE_EFFECT, p[6], now);
        return 1;
    case 0x8009:                                       /* timer: 0 stop, 1 start, 2 pause */
        if (len < 5) return 0;
        if (p[4] == 1 && S.tstate != 1) { S.tstart = now; S.tstate = 1; }
        else if (p[4] == 2 && S.tstate == 1) { S.tacc += now - S.tstart; S.tstate = 2; }
        else if (p[4] == 0) { S.tacc = 0; S.tstate = 0; }
        modes_set(MODE_TIMER, 0, now);
        return 1;
    case 0x800A:                                       /* scores */
        if (len < 7) return 0;
        S.s1 = (uint8_t)(p[4] % 100); S.s2 = (uint8_t)(p[6] % 100);
        modes_set(MODE_SCORE, 0, now);
        return 1;
    case 0x0104:                                       /* fun mode on/off -> drawing canvas */
        if (len < 5) return 0;
        if (p[4]) {
            clear_canvas();
            modes_set(MODE_CANVAS, 0, now);
        } else {
            modes_set(MODE_EFFECT, S.effect, now);
        }
        return 1;
    case 0x0105:                                       /* set pixel: 0 R G B x y */
        if (len < 10 || p[8] > 31 || p[9] > 31) return 0;
        canvas[p[9]][p[8]][0] = p[5]; canvas[p[9]][p[8]][1] = p[6]; canvas[p[9]][p[8]][2] = p[7];
        if (S.mode != MODE_CANVAS) modes_set(MODE_CANVAS, 0, now);
        return 1;
    case 0x0106:                                       /* clock: style 24h show_date yy mm dd dow */
        if (len < 7) return 0;
        S.fmt24 = p[5] ? 1 : 0; S.show_date = p[6] ? 1 : 0;
        if (len >= 11) clock_set_date(p[7], p[8], p[9], p[10]);
        modes_set(MODE_CLOCK, p[4], now);
        return 1;
    case 0x0107:                                       /* power */
        if (len < 5) return 0;
        S.power = p[4] ? 1 : 0;
        return 1;
    case 0x0200:                                       /* rhythm animation: t style */
        if (len < 6) return 0;
        S.ranimated = 1; S.lvl[0] = (uint8_t)(p[4] & 7u); S.rstyle = p[5];
        modes_set(MODE_RHYTHM, 0, now);
        return 1;
    case 0x0201:                                       /* rhythm levels: style l1..l11 */
        if (len < 16) return 0;
        S.ranimated = 0; S.rstyle = p[4];
        for (int i = 0; i < 11; i++) S.lvl[i] = (uint8_t)(p[5 + i] & 15u);
        modes_set(MODE_RHYTHM, 0, now);
        return 1;

    /* ---- our own commands ---- */
    case 0xF101:                                       /* mode: mode arg */
        if (len < 6 || p[4] >= MODE_COUNT) return 0;
        if (p[4] == MODE_EFFECT) S.autoplay = 0;
        modes_set(p[4], p[5], now);
        return 1;
    case 0xF102:                                       /* text: r g b speed flags text... */
        if (len < 10) return 0;
        modes_set_text((const char *)p + 9, len - 9, RGB(p[4], p[5], p[6]), p[7], p[8], now);
        return 1;
    case 0xF10A:                                       /* 99.14 single text strip -> one-message playlist */
        if (!textfx_legacy_strip(p, len)) return 0;
        set_text_style(RGB(p[4], p[5], p[6]), p[7], p[8]);
        S.text_kind = 1;
        modes_set(MODE_TEXT, 0, now);
        return 1;
    case 0xF10B: {                                     /* message playlist, one message per packet (textfx.h) */
        int r = textfx_add(p, len);
        if (!r) return 0;
        if (r == 2) { S.text_kind = 1; modes_set(MODE_TEXT, 0, now); }
        return 1; }
    case 0xF103:                                       /* full frame 32x32 RGB */
        if (len != 4u + GFX_W * GFX_H * 3u) return 0;
        for (int y = 0; y < GFX_H; y++) for (int x = 0; x < GFX_W; x++)
            for (int c = 0; c < 3; c++) canvas[y][x][c] = p[4 + (y * GFX_W + x) * 3 + c];
        modes_set(MODE_CANVAS, 0, now);
        return 1;
    case 0xF104:                                       /* autoplay: on seconds_lo seconds_hi */
        if (len < 7) return 0;
        S.autoplay = p[4] ? 1 : 0;
        S.autoplay_s = (uint16_t)(p[5] | (p[6] << 8));
        if (S.autoplay_s < 3) S.autoplay_s = 3;
        if (S.autoplay) modes_set(MODE_EFFECT, S.effect, now);
        return 1;
    case 0xF105:                                       /* pixels: n x (x y r g b) */
        if (len < 9 || (len - 4u) % 5u) return 0;
        for (unsigned i = 4; i + 5 <= len; i += 5)
            if (p[i] < GFX_W && p[i + 1] < GFX_H) {
                canvas[p[i + 1]][p[i]][0] = p[i + 2]; canvas[p[i + 1]][p[i]][1] = p[i + 3]; canvas[p[i + 1]][p[i]][2] = p[i + 4];
            }
        if (S.mode != MODE_CANVAS) modes_set(MODE_CANVAS, 0, now);
        return 1;
    case 0xF106:                                       /* status */
        reply_status(now);
        return 2;
    case 0xF107:                                       /* press the key remotely: 1 short, 2 long */
        if (len < 5) return 0;
        modes_key(p[4], now);
        return 1;
    case 0xF108:                                       /* factory settings: F108 A5 (saved a few s later) */
        if (len < 5 || p[4] != 0xA5) return 0;
        modes_defaults(now);
        return 1;
    case ANIM_CMD_PLAY:                                /* play a stored animation: slot (ANIM_ALL, ANIM_KEEP) speed */
        if (len < 6) return 0;
        if (p[5]) S.anim_speed = (uint8_t)(p[5] < 2 ? 2 : p[5] > 40 ? 40 : p[5]);
        if (p[4] == ANIM_KEEP) return 1;
        if (!anim_valid(p[4])) return 0;
        modes_set(MODE_ANIM, p[4], now);
        return 1;
    default: {                                         /* animation store: upload, list, info, delete */
        int r = anim_cmd(cmd, p, len, now);
        if (r < 0) return -1;
        if (r & ANIM_PLAY_REQ) modes_set(MODE_ANIM, (unsigned)r & 0xFFu, now);
        return 2; }
    }
}
