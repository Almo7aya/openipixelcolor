#pragma once
#include <stdint.h>

/* Logical framebuffer: fb[y][x][r,g,b], (0,0) top-left as the vendor app content is drawn.
 * Orientation 1 (vendor default) maps it onto the panel; change with display_set_orientation(). */
void     display_init(void);
void     display_set_brightness(unsigned level);   /* 0..15, live */
void     display_set_orientation(unsigned o);      /* 0..3 */
void     display_set_gamma(int on);
int      display_show(const uint8_t fb[32][32][3]);  /* 0 if the previous frame is still pending */
void     display_show_wait(const uint8_t fb[32][32][3]);
void     display_blank(void);
void     display_suspend(void);                      /* stop scanning (before flash writes), until reboot */
uint32_t display_rows_scanned(void);
