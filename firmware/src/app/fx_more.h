/* Effects in fx_more.c (listed in the effects[] table in effects.c). */
#pragma once
#include <stdint.h>

void fx_nop_init(void);
void aurora_frame(uint32_t t);
void lava_frame(uint32_t t);
void rain_init(void);       void rain_frame(uint32_t t);
void dna_frame(uint32_t t);
void warp_init(void);       void warp_frame(uint32_t t);
void radar_init(void);      void radar_frame(uint32_t t);
