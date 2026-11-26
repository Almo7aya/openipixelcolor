/* Small helpers for the effect and clock files (static inline: no RAM, no shared state). */
#pragma once
#include <stdint.h>
#include "gfx.h"
#include "tables.h"

#define SN(a) ((int)sin8[(uint8_t)(a)] - 128)                   /* sine, -128..127, a full turn = 256 */
#define CS(a) ((int)sin8[(uint8_t)((a) + 64)] - 128)

static inline rgb_t fx_mix(rgb_t a, rgb_t b, int k)              /* k = 0..256: a -> b */
{
    int ar = (int)(a >> 16) & 255, ag = (int)(a >> 8) & 255, ab = (int)a & 255;
    int br = (int)(b >> 16) & 255, bg = (int)(b >> 8) & 255, bb = (int)b & 255;
    return RGB(ar + (br - ar) * k / 256, ag + (bg - ag) * k / 256, ab + (bb - ab) * k / 256);
}

static inline unsigned fx_isqrt(unsigned v)
{
    unsigned r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1;
        b >>= 2;
    }
    return r;
}

static inline uint32_t fx_hash(uint32_t v) { v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15; v *= 0x846ca68bu; return v ^ (v >> 16); }
