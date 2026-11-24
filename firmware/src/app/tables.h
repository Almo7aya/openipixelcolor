#pragma once
#include <stdint.h>

extern const uint8_t sin8[256];        /* 128 + 127 sin */
extern const uint8_t ang32[32][32];    /* angle around centre, 0..255 */
extern const uint8_t dist32[32][32];   /* distance from centre, 1/8 px */
extern const uint8_t font5x7[95][5];   /* ASCII 32..126, column bytes, bit0 = top row */

#define cos8(i) sin8[(uint8_t)((i) + 64)]
