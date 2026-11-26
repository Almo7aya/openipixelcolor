/* THE FINALS emblem as pixel masks (tools/gen_emblem.py), for the fan-made animations in fx_finals.c.
 * THE FINALS and its emblem belong to Embark Studios. Row y, pixel x: (rows[y] >> (W - 1 - x)) & 1 */
#pragma once
#include <stdint.h>

#define EMBLEM_BIG_W   28
#define EMBLEM_BIG_H   17
#define EMBLEM_SMALL_W 18
#define EMBLEM_SMALL_H 11

extern const uint32_t emblem_big[EMBLEM_BIG_H];
extern const uint32_t emblem_small[EMBLEM_SMALL_H];
