#pragma once
#include <stdint.h>

void     dma_ch_disable(unsigned c);
void     dma_ch_enable(unsigned c);
void     dma_ring_rx_setup(unsigned c, unsigned req, uint32_t periph_addr, void *ring, uint16_t size);
unsigned dma_ring_available(unsigned c, uint16_t size);
unsigned dma_ring_read(unsigned c, uint16_t size, void *dst, unsigned n);
