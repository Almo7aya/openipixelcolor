/* MCU firmware update over the companion link - byte-compatible with the vendor firmware (cmd 0xC000 / 0xC001).
 * Hardware-independent so it can be unit-tested on the PC (tools/test_ota). */
#pragma once
#include <stdint.h>

#define OTA_STAGING   0x00108000u
#define OTA_RECORD    0x001FF000u
#define OTA_CHUNK     0x3000u
#define OTA_MAX_SIZE  0xF7800u

/* status codes returned to the sender (byte 4 of the reply) */
#define OTA_OK        1u
#define OTA_FAIL      0u

int      ota_begin(uint8_t count, uint32_t crc_encoded, uint32_t size);
/* pkt = whole 0xC001 frame (len|cmd|idx|crc|len|data), pkt_len = its length */
int      ota_chunk(const uint8_t *pkt, uint32_t pkt_len);
int      ota_active(void);
void     ota_abort(void);
void     ota_progress(uint32_t *done, uint32_t *total);
uint32_t ota_crc32(uint32_t crc, const void *buf, uint32_t len);

/* --- provided by the platform (firmware: proto.c, PC test: mocks) --- */
int  ota_port_flash_write(uint32_t addr, const void *buf, uint32_t len);
const void *ota_port_flash_ptr(uint32_t addr);   /* memory-mapped read of flash (XIP) for verification */
void ota_port_final_ack(void);    /* send 05 00 01 C0 01 before the record write + reset */
void ota_port_reset(void);        /* never returns on hardware */
