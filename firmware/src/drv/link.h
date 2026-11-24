#pragma once
#include <stdint.h>

#define LINK_MAX_PACKET 0x3080u   /* ring size; the largest command (an update chunk) is 0x3009 bytes */

void     link_init(void);
unsigned link_poll(const uint8_t **pkt);     /* -> length incl. 4-byte header, 0 if none */
void     link_reply(uint16_t cmd, uint8_t status, const uint8_t *extra, unsigned extra_len);

static inline uint16_t link_cmd(const uint8_t *pkt) { return (uint16_t)(pkt[2] | (pkt[3] << 8)); }
