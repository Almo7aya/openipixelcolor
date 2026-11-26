/* The panel this build is for. iPixel Color panels come in several matrix sizes; openipixelcolor runs on the
 * P2064AF2 board, which drives a 32 x 32 matrix. Apps learn the size when they connect: the 0x8001 reply
 * carries the vendor's device type byte, and 0xF113 (panel info) gives width, height and board.
 * Everything that draws (effects, clocks, text, screens, the animation player) uses GFX_W x GFX_H from here;
 * much of it is laid out for 32 x 32 (screens.c checks that at build time), so a board with another size
 * also needs those layouts checked. Build for another board with -DBOARD_W=.. -DBOARD_H=.. etc. */
#pragma once

#ifndef BOARD_W
#define BOARD_W            32
#define BOARD_H            32
#define BOARD_ID           1        /* 1 = P2064AF2 (MVSilicon, NDS32), 32 x 32 */
#define BOARD_DEVICE_TYPE  0x81u    /* the vendor's device type byte for this panel (0x8001 reply) */
#endif

#define FW_CMD_PANEL_INFO  0xF113u  /* reply: w(2) h(2) board type */
#define FW_CMD_XFLASH      0xF114u  /* external flash probe (proto.c) */
