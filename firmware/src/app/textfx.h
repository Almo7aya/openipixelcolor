/* Text engine: a playlist of up to 4 messages, each with an entry effect, a hold effect, an exit
 * effect, a colour mode, decorations and a background (black, a colour, or any animation dimmed).
 * Messages are either a picture strip drawn by the app (any script, Arabic too) or text in the panel
 * font with live tokens {time} {secs} {date} {day} {year}. Hardware independent (runs in the simulator).
 *
 * cmd 0xF10B, one message per packet:
 *   idx count kind style[16] then  kind 0 (strip): h w_lo w_hi columns (ceil(h/8) bytes each, bit 0 = top)
 *                                   kind 1 (font):  scale(0 = 7 px, 1 = 14 px) ASCII text with tokens
 *   idx 0 clears the playlist; messages must come in order; the last one (idx = count - 1) starts it.
 * style: 0 enter 1 hold 2 exit 3 colour mode 4-6 colour 1 7-9 colour 2 10 speed 1-10 11 stay (1/4 s, 0 = 2 s)
 *        12 align (0 middle 1 top 2 bottom) | rtl << 2 | decor << 4 (1 shadow 2 outline 4 glow 8 rails)
 *        13 background (0 black, 1 colour 2, 2 + n = effect n) 14 background brightness 15 repeat */
#pragma once
#include <stdint.h>

#define TEXTFX_MSGS        4u
#define TEXTFX_POOL        3584u
#define TEXTFX_STYLE       16u
#define TEXTFX_SERIAL_MAX  (108u + TEXTFX_POOL)

void textfx_reset(void);
int  textfx_count(void);
int  textfx_add(const uint8_t *pkt, unsigned len);          /* 0xF10B: 0 refused, 1 stored, 2 stored and complete */
int  textfx_legacy_strip(const uint8_t *pkt, unsigned len); /* 0xF10A (99.14 single strip): 1 ok */
void textfx_start(uint32_t now);                            /* play from the first message */
void textfx_render(uint32_t now);                           /* draws into the current gfx target */
const uint8_t *textfx_serial(unsigned *len);                /* for saving; NULL when empty */
int  textfx_load(const uint8_t *p, unsigned len);           /* saved playlist or a 99.14 strip */
