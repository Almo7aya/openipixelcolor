/* Settings and picture that survive a power cycle (see settings.c). */
#pragma once
#include <stdint.h>

#define SETTINGS_SECTOR 0x001FD000u     /* 16 slots x 256 B, newest valid record wins */
#define PICTURE_SECTOR  0x001FC000u     /* one 32x32 picture */
#define TEXTBITS_SECTOR 0x001FB000u     /* text as a picture strip (Arabic etc.) */

void     settings_load(uint32_t now, int apply);    /* once at boot, right after modes_init() */
void     settings_tick(uint32_t now, int allowed);  /* main loop; writes flash only when allowed */
unsigned settings_info(uint8_t out[8]);              /* status for cmd 0xF109, returns 8 */
