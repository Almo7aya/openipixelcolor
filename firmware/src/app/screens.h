/* Screens the panel shows by itself (screens.c): the logo, boot, first-start welcome, firmware update, an app
 * connecting, an animation being saved. Hardware independent: the simulator renders them too. */
#pragma once
#include <stdint.h>
#include "gfx.h"

#define SCREENS_BOOT_MS      3500u      /* the boot screen, then the saved mode */
#define SCREENS_CONNECTED_MS 1400u      /* shown over whatever plays when an app connects */
#define SCREENS_SAVED_MS     1200u      /* "saved", then the new animation plays */

void screens_logo(void);                                /* the logo, still (README, favicons) */
void screens_boot(uint32_t t);                          /* t = ms since start, 0..SCREENS_BOOT_MS */
void screens_welcome(uint32_t now);                     /* first start: until something is chosen */
void screens_update(fb_t *fb, uint32_t now, uint32_t done, uint32_t total);   /* firmware update */
void screens_connected(uint32_t t);                     /* t = ms since the app connected */
/* an animation upload: done of total bytes; done_ms >= 0: finished that long ago ("saved") */
void screens_saving(uint32_t now, uint32_t done, uint32_t total, int done_ms);
