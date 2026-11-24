/* Tiny graphics library for the 32x32 RGB framebuffer (hardware independent, also used by the PC sim). */
#pragma once
#include <stdint.h>

#define GFX_W 32
#define GFX_H 32

typedef uint8_t fb_t[GFX_H][GFX_W][3];
typedef uint32_t rgb_t;                         /* 0xRRGGBB */

#define RGB(r, g, b) ((rgb_t)(((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b)))

void     gfx_target(fb_t *fb);
fb_t    *gfx_fb(void);
void     gfx_fill(rgb_t c);
void     gfx_fade(uint8_t keep);               /* every channel *= keep/256 (trails) */
void     gfx_px(int x, int y, rgb_t c);
void     gfx_px_add(int x, int y, rgb_t c);    /* saturating add */
rgb_t    gfx_get(int x, int y);
void     gfx_line(int x0, int y0, int x1, int y1, rgb_t c);
void     gfx_rect(int x, int y, int w, int h, rgb_t c, int fill);
void     gfx_circle(int cx, int cy, int r, rgb_t c, int fill);
rgb_t    gfx_hsv(uint8_t h, uint8_t s, uint8_t v);
rgb_t    gfx_scale(rgb_t c, uint8_t v);        /* c * v/255 */

/* 5x7 proportional font (ASCII 32..126) */
int      gfx_char(int x, int y, char ch, rgb_t c);          /* returns advance */
int      gfx_text(int x, int y, const char *s, rgb_t c);    /* returns x after the text */
int      gfx_text_width(const char *s);
/* 3x5 font: 0-9, A-Z (a-z drawn as A-Z), space : . - ! ? ' / % + $; proportional, 1 px gap */
int      gfx_tiny_char(int x, int y, char ch, rgb_t c);     /* returns advance */
int      gfx_tiny_text(int x, int y, const char *s, rgb_t c);
int      gfx_tiny_width(const char *s);
/* 7-segment digit (0..9, 10 = blank, 11 = '-') in a w x h box, segment thickness th */
void     gfx_7seg(int x, int y, int w, int h, int th, int digit, rgb_t c);
/* drawing (gfx_px / gfx_px_add and everything built on them) limited to [x0,x1) x [y0,y1) */
void     gfx_clip(int x0, int y0, int x1, int y1);
void     gfx_noclip(void);

uint32_t gfx_rand(void);
uint32_t gfx_rnd(uint32_t n);                  /* 0..n-1 */
void     gfx_seed(uint32_t s);
