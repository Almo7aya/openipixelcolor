#pragma once
#include <stdint.h>

typedef struct {
    const char *name;
    void (*init)(void);
    void (*frame)(uint32_t t);      /* t = milliseconds, called ~40x per second */
} effect_t;

extern const effect_t effects[];
extern const unsigned effect_count;

/* State memory shared by all effects (only one runs at a time; its init() resets what it uses). */
#define FX_SCRATCH_BYTES 4096u
void *fx_scratch(void);

/* shared by the rhythm/equalizer mode: 11 bars, levels 0..15 */
void eq_draw(const uint8_t lvl[11], unsigned style, uint32_t t);
