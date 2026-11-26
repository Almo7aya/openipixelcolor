/* Clock faces 10.. (clockface2.c); clockface.c draws 0..9 and hands the rest over. */
#pragma once
#include <stdint.h>
#include "clockface.h"

#define CLOCK2_STYLES 16

/* style: 0 = clock face 10. hh: the hour as shown (12 or 24 hour); ms: milliseconds into the second */
void clock2_draw(unsigned style, const clock_tm_t *tm, int hh, int fmt24, int show_date, uint32_t now, unsigned ms);
