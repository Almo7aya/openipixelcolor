/* Effects in fx_demo.c (listed in the effects[] table in effects.c). */
#pragma once
#include <stdint.h>

void rotozoom_frame(uint32_t t);
void twister_frame(uint32_t t);
void copper_frame(uint32_t t);
void bobs_init(void);       void bobs_frame(uint32_t t);
void kaleido_frame(uint32_t t);
void julia_frame(uint32_t t);
void moire_frame(uint32_t t);
void sunburst_frame(uint32_t t);
void spiro_init(void);      void spiro_frame(uint32_t t);
void cube_init(void);       void cube_frame(uint32_t t);
void synth_frame(uint32_t t);
void terrain_init(void);    void terrain_frame(uint32_t t);
void globe_init(void);      void globe_frame(uint32_t t);
void tartan_frame(uint32_t t);
