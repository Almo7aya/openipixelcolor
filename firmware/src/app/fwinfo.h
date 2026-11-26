/* Version + boot/rescue state shared by main.c and proto.c */
#pragma once
#include <stdint.h>

/* Reported by cmd 0x8005 as "MCU major.minor". The vendor firmware reports 3.x; this one reports 99.xx so
 * the two can never be confused (tools and the web UI rely on it). */
#define FW_VER_MAJOR  99u
#define FW_VER_MINOR  18u
#define FW_VERSION_STR "99.18"   /* 99.02: flash status read order fixed (99.01 could not write flash)
                                    99.03: minimum erase/program waits, display stopped during updates
                                    99.10: effects, clocks, text, canvas, rhythm/score/timer, BLE app cmds
                                    99.11: default orientation 2, richer status reply
                                    99.12: settings + picture survive a power cycle, 0xF108 / 0xF109
                                    99.13: 5 more clock faces, 6 more effects
                                    99.14: text in any script as a picture strip (0xF10A), settings layout 2
                                    99.17: text engine: playlist of 4 messages, 22 entry/exit effects,
                                           10 hold effects, 9 colour modes, decorations, animated
                                           backgrounds, live {time} tokens (0xF10B, textfx.c)
                                    99.18: animations uploaded over Bluetooth, stored in flash and played
                                           by the panel (0xF10C-0xF111, anim.c), settings layout 3 */

/* Replies travel MCU -> UART2 -> BLE chip -> notify; the BLE chip forwards at most 16 bytes (seen on HW).
 * openpanel32 diagnostics command; reply (16 B): 10 00 F1 F0 01 mode crash exc_type ipc(4) uptime_s(4) */
#define FW_CMD_DIAG   0xF0F1u

enum { FW_MODE_NORMAL = 0, FW_MODE_RESCUE_KEY = 1, FW_MODE_RESCUE_CRASH = 2 };

unsigned fw_mode(void);
unsigned fw_crash_count(void);
unsigned fw_last_exc_type(void);
uint32_t fw_last_exc_ipc(void);
void     fw_prepare_reset(void);   /* intentional reset: do not count it as a crash */
