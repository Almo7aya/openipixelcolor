/* Version + boot/rescue state shared by main.c and proto.c */
#pragma once
#include <stdint.h>

/* The version is one number: 100, 101, 102, ... Every firmware release gets the next one: the CI takes it
 * from the release tags (v100, v101, ...) and builds with FW_VERSION=<n>; local builds do the same from the
 * local tags (Makefile). Cmd 0x8005 reports it where the vendor firmware puts "MCU major.minor":
 * major = the number, minor 0 (from 256 on: major 255, minor = number - 255). The vendor firmware reports
 * 3.x and the builds before the single numbers 99.xx, so the three can never be confused. */
#ifndef FW_VERSION
#define FW_VERSION 100
#endif
#define FW_VER_MAJOR   ((FW_VERSION) > 255 ? 255u : (unsigned)(FW_VERSION))
#define FW_VER_MINOR   ((FW_VERSION) > 255 ? (unsigned)(FW_VERSION) - 255u : 0u)
#define FW_STR_(x)     #x
#define FW_STR(x)      FW_STR_(x)
#define FW_VERSION_STR FW_STR(FW_VERSION)

/* Before the single numbers (major.minor):
                                    99.02: flash status read order fixed (99.01 could not write flash)
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
                                           by the panel (0xF10C-0xF111, anim.c), settings layout 3
                                    99.19: logo boot screen, welcome screen on the first start (mode 10),
                                           update screen instead of a dark panel during updates (screens.c)
   From 100 on, the changes are in the release notes on GitHub. */

/* Replies travel MCU -> UART2 -> BLE chip -> notify; the BLE chip forwards at most 16 bytes (seen on HW).
 * openipixelcolor diagnostics command; reply (16 B): 10 00 F1 F0 01 mode crash exc_type ipc(4) uptime_s(4) */
#define FW_CMD_DIAG   0xF0F1u

enum { FW_MODE_NORMAL = 0, FW_MODE_RESCUE_KEY = 1, FW_MODE_RESCUE_CRASH = 2 };

unsigned fw_mode(void);
unsigned fw_crash_count(void);
unsigned fw_last_exc_type(void);
uint32_t fw_last_exc_ipc(void);
void     fw_prepare_reset(void);   /* intentional reset: do not count it as a crash */
