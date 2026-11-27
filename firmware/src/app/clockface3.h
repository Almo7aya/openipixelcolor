/* Clock faces 26.. (clockface3.c); clockface.c draws 0..9, clockface2.c 10..25. */
#pragma once
#include <stdint.h>
#include "clockface.h"

#define CLOCK3_FIRST  26
#define CLOCK3_STYLES 17

/* style: 0 = clock face 26. hh: the hour as shown (12 or 24 hour); ms: milliseconds into the second */
void clock3_draw(unsigned style, const clock_tm_t *tm, int hh, int fmt24, int show_date, uint32_t now, unsigned ms);
