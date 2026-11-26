/* Screens the panel shows by itself: the logo, the boot screen, the first-start welcome and the
 * firmware-update screen (screens.c). Hardware independent: the simulator renders them too. */
#pragma once
#include <stdint.h>
#include "gfx.h"

#define SCREENS_BOOT_MS 1800u

/* the logo into the gfx target: an open colour ring around a pixel "i", one pixel escaping through the
 * gap. cy2 = twice the centre row (32: centred). reveal 0..256 draws the ring clockwise from the gap,
 * spin turns the colours, escape 0..256 moves the loose pixel out, ink 0..255 dims the "i" */
void screens_logo(int cy2, unsigned reveal, unsigned spin, unsigned escape, unsigned ink);

void screens_boot(uint32_t t);                       /* t = ms since start, 0..SCREENS_BOOT_MS */
void screens_welcome(uint32_t now);                  /* first start: until something is chosen */
void screens_update(fb_t *fb, uint32_t now, uint32_t done, uint32_t total);   /* firmware update */
