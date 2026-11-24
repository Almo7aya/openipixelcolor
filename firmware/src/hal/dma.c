/* DMA controller at 0x40008000: 8 physical channels x 0x20, enable bitmap at +0x100
 * (docs/HARDWARE.md). Only what the drivers need. */
#include "dma.h"
#include "regs.h"

#define DMA_CH(c, off)  REG32(0x40008000u + (uint32_t)(c) * 0x20u + (off))
#define DMA_CH16(c, off) REG16(0x40008000u + (uint32_t)(c) * 0x20u + (off))
#define DMA_ENABLE      REG32(0x40008100u)
#define DMA_CLK_GATE    (1u << 15)

void dma_ch_disable(unsigned c) { DMA_ENABLE &= ~(1u << c); }
void dma_ch_enable(unsigned c)  { DMA_ENABLE |= (1u << c); }

/* peripheral -> memory, 8-bit, destination increments, circular */
void dma_ring_rx_setup(unsigned c, unsigned req, uint32_t periph_addr, void *ring, uint16_t size)
{
    dma_ch_disable(c);
    for (unsigned off = 0; off < 0x20u; off += 4u) DMA_CH(c, off) = 0;
    REG32(CLK_GATE0) |= DMA_CLK_GATE;
    DMA_CH(c, 0x00) = (req & 0x1Fu) | (1u << 14) | (1u << 21) | (1u << 22);
    DMA_CH(c, 0x10) |= 1u << 16;
    DMA_CH16(c, 0x10) = size;
    DMA_CH16(c, 0x04) = size;
    DMA_CH(c, 0x14) = periph_addr;
    DMA_CH(c, 0x18) = (uint32_t)(uintptr_t)ring;
    dma_ch_enable(c);
}

unsigned dma_ring_available(unsigned c, uint16_t size)
{
    uint32_t hw = DMA_CH16(c, 0x08);
    uint32_t sw = DMA_CH16(c, 0x0C);
    return (size + hw - sw) % size;
}

unsigned dma_ring_read(unsigned c, uint16_t size, void *dst, unsigned n)
{
    const uint8_t *ring = (const uint8_t *)(uintptr_t)DMA_CH(c, 0x18);
    uint8_t *d = dst;
    unsigned avail = dma_ring_available(c, size);
    uint32_t sw = DMA_CH16(c, 0x0C);
    if (n > avail) n = avail;
    for (unsigned i = 0; i < n; i++) {
        d[i] = ring[sw];
        if (++sw == size) sw = 0;
    }
    DMA_CH16(c, 0x0C) = (uint16_t)sw;
    return n;
}
