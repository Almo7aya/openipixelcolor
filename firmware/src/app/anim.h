/* Animations uploaded over Bluetooth, kept in flash and played by the panel on its own (see anim.c and
 * docs/PROTOCOL.md "Animations"). Hardware independent (storage via app_port.h): the PC simulator runs it. */
#pragma once
#include <stdint.h>

/* 20 slots of 32 KiB at the top of the update staging area, below the settings sectors (0x1FB000+).
 * An animation larger than one slot uses the following slots too. A firmware update only reaches this
 * area with an image larger than 0x48000 bytes (openpanel32 ~45 KiB, the vendor's ~255 KiB); the boot
 * scan then finds the damaged animations by their CRC and leaves them out. */
#define ANIM_BASE       0x00150000u
#define ANIM_SLOT_SIZE  0x8000u
#define ANIM_SLOTS      20u
#define ANIM_ALL        0xFFu          /* "slot" meaning: every stored animation in turn */
#define ANIM_KEEP       0xFEu          /* 0xF111: change the speed only */
#define ANIM_W          32u
#define ANIM_H          32u

/* commands (0xF10C-0xF110 handled by anim_cmd, 0xF111 play by modes.c) */
#define ANIM_CMD_BEGIN  0xF10Cu
#define ANIM_CMD_DATA   0xF10Du
#define ANIM_CMD_LIST   0xF10Eu
#define ANIM_CMD_INFO   0xF10Fu
#define ANIM_CMD_DELETE 0xF110u
#define ANIM_CMD_PLAY   0xF111u

#define ANIM_PLAY_REQ   0x100          /* anim_cmd result flag: replied, and the slot in the low byte should play */

void     anim_init(void);                                   /* at boot: scan the store (plain memory reads) */
int      anim_cmd(uint16_t cmd, const uint8_t *p, unsigned len, uint32_t now);   /* -1 = not ours, else replied */
void     anim_abort_upload(void);                           /* a firmware update starts */
void     anim_tick(uint32_t now);                           /* upload timeout */
int      anim_uploading(void);
void     anim_draw_upload(void);                            /* progress screen while an upload runs */

int      anim_valid(unsigned slot);                         /* complete animation there (ANIM_ALL: any) */
void     anim_start(unsigned slot, uint32_t now);           /* slot or ANIM_ALL */
void     anim_draw(uint32_t now, unsigned speed10);         /* into the gfx target; speed10: 10 = normal */
unsigned anim_showing(void);                                /* slot on screen, 0xFF = none */
int      anim_next(uint32_t now);                           /* key: next animation; 0 = there is none after this one */
