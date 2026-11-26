/* Effects in fx_sim.c (listed in the effects[] table in effects.c). */
#pragma once
#include <stdint.h>

void ocean_frame(uint32_t t);
void sand_init(void);       void sand_frame(uint32_t t);
void ants_init(void);       void ants_frame(uint32_t t);
void flock_init(void);      void flock_frame(uint32_t t);
void galaxy_frame(uint32_t t);
void snow_init(void);       void snow_frame(uint32_t t);
void aqua_init(void);       void aqua_frame(uint32_t t);
void ffly_init(void);       void ffly_frame(uint32_t t);
void maze_init(void);       void maze_frame(uint32_t t);
void sort_init(void);       void sort_frame(uint32_t t);
void pend_init(void);       void pend_frame(uint32_t t);
void brk_init(void);        void brk_frame(uint32_t t);
void meadow_frame(uint32_t t);
