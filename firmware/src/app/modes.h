#pragma once
#include <stdint.h>
#include "gfx.h"
#include "textfx.h"

enum {
    MODE_EFFECT = 0, MODE_CLOCK, MODE_TEXT, MODE_CANVAS, MODE_RHYTHM, MODE_SCORE, MODE_TIMER, MODE_TEST,
    MODE_SPLASH, MODE_ANIM, MODE_WELCOME, MODE_COUNT
};

enum { MKEY_SHORT = 1, MKEY_LONG = 2 };

void modes_init(uint32_t now);
void modes_render(fb_t *fb, uint32_t now);
void modes_key(int ev, uint32_t now);
void modes_set(unsigned mode, unsigned arg, uint32_t now);
void modes_set_time(uint8_t h, uint8_t m, uint8_t s, uint32_t now);
void modes_set_text(const char *s, unsigned len, uint32_t rgb, unsigned speed, unsigned flags, uint32_t now);
void modes_first_start(int yes);   /* nothing saved yet: the welcome screen follows the boot screen */
/* BLE commands: returns -1 = not handled, 0/1 = status to acknowledge, 2 = handled and already replied */
int  modes_handle_cmd(uint16_t cmd, const uint8_t *pkt, unsigned len, uint32_t now);

/* What survives a power cycle (saved by settings.c). Bytes only, so the layout has no padding and is the
 * same on the panel and in the PC simulator. Bump MODES_PERSIST_VER whenever the layout changes. */
#define MODES_PERSIST_VER    3u     /* 2: + text_kind (99.14), 3: + anim_slot, anim_speed (99.18) */
#define MODES_PERSIST_V1_LEN 152u   /* records of layout 1 are still accepted */
#define MODES_TEXT_MAX       120u
#define MODES_TEXTBITS_MAX   TEXTFX_SERIAL_MAX   /* saved message playlist (textfx.h) */
typedef struct {
    uint8_t ver;
    uint8_t mode;                  /* last "lasting" mode: effect, clock, text, canvas, rhythm or score */
    uint8_t effect, autoplay, autoplay_lo, autoplay_hi;
    uint8_t bright_pct, orient;
    uint8_t clock_style, fmt24, show_date;
    uint8_t text_r, text_g, text_b, text_rainbow, text_speed;
    uint8_t rstyle, ranimated, lvl[11];
    uint8_t s1, s2;
    char    text[MODES_TEXT_MAX + 1];
    /* layout 2 */
    uint8_t text_kind;             /* 0 = text in the built-in font, 1 = message playlist (textfx.c) */
    /* layout 3 */
    uint8_t anim_slot;             /* animation to play (anim.h), ANIM_ALL = all in turn */
    uint8_t anim_speed;            /* 10 = as recorded, 2..40 */
} modes_persist_t;

void modes_snapshot(modes_persist_t *p);
void modes_restore(const modes_persist_t *p, uint32_t now);   /* every field is range-checked */
void modes_defaults(uint32_t now);                            /* factory settings, blank picture */
const uint8_t *modes_canvas(void);                            /* 32*32*3 bytes, [y][x][rgb] */
void modes_restore_canvas(const uint8_t *px);
const uint8_t *modes_textbits(unsigned *len);                 /* NULL unless the text is a picture strip */
int  modes_restore_textbits(const uint8_t *p, unsigned len);  /* 1 = valid and taken */
