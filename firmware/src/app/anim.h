/* Animations uploaded over Bluetooth, kept in flash and played by the panel on its own (see anim.c and
 * docs/PROTOCOL.md "Animations"). Hardware independent (storage via app_port.h): the PC simulator runs it. */
#pragma once
#include <stdint.h>
#include "board.h"

/* Slots of 32 KiB; an animation larger than one slot uses the following slots too.
 * With the external flash (anim.c, docs/HARDWARE.md): 127 slots from ANIM_XBASE, the chip marked as ours at
 * ANIM_XMARK, sector 0 left erased. Without it: 20 slots at the top of the internal update staging area,
 * below the settings sectors (0x1FB000+); a firmware update only reaches them with an image larger than
 * 0x48000 bytes, and the boot scan then leaves the damaged ones out by their CRC. */
#define ANIM_BASE       0x00150000u    /* internal flash */
#define ANIM_SLOT_SIZE  0x8000u
#define ANIM_SLOTS_INT  20u
#define ANIM_XMARK      0x00001000u    /* external flash: "ours" + layout */
#define ANIM_XBASE      0x00008000u    /* external flash: slot 0 */
#define ANIM_SLOTS_MAX  127u           /* external flash: up to 4 MiB */
#define ANIM_ALL        0xFFu          /* "slot" meaning: every stored animation in turn */
#define ANIM_KEEP       0xFEu          /* 0xF111: change the speed only */
#define ANIM_W          ((unsigned)BOARD_W)   /* a file must be made for this panel's size (header w, h) */
#define ANIM_H          ((unsigned)BOARD_H)

/* commands (0xF10C-0xF110 and 0xF112 handled by anim_cmd, 0xF111 play by modes.c) */
#define ANIM_CMD_BEGIN  0xF10Cu
#define ANIM_CMD_DATA   0xF10Du
#define ANIM_CMD_LIST   0xF10Eu
#define ANIM_CMD_INFO   0xF10Fu
#define ANIM_CMD_DELETE 0xF110u
#define ANIM_CMD_PLAY   0xF111u
#define ANIM_CMD_READ   0xF112u

#define ANIM_PLAY_REQ   0x100          /* anim_cmd result flag: replied, and the slot in the low byte should play */

void     anim_init(void);                                   /* at boot: find the store and scan it */
int      anim_cmd(uint16_t cmd, const uint8_t *p, unsigned len, uint32_t now);   /* -1 = not ours, else replied */
void     anim_abort_upload(void);                           /* a firmware update starts */
void     anim_tick(uint32_t now);                           /* upload timeout; preparing the external flash */
int      anim_uploading(void);
int      anim_draw_saving(uint32_t now, int playing);       /* saving screen during an upload, then "saved"; 1 = drew */

int      anim_valid(unsigned slot);                         /* complete animation there (ANIM_ALL: any) */
void     anim_start(unsigned slot, uint32_t now);           /* slot or ANIM_ALL */
void     anim_draw(uint32_t now, unsigned speed10);         /* into the gfx target; speed10: 10 = normal */
unsigned anim_showing(void);                                /* slot on screen, 0xFF = none */
int      anim_next(uint32_t now);                           /* key: next animation; 0 = there is none after this one */
