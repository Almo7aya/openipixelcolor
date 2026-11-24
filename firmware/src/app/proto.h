#pragma once
#include <stdint.h>

void proto_handle(const uint8_t *pkt, unsigned len);
void proto_tick(void);      /* call from the main loop: OTA inactivity reset */
