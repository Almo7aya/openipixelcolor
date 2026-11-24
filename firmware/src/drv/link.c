/* Link to the Wi-Fi/BLE companion module (docs/HARDWARE.md, docs/PROTOCOL.md).
 *   module -> MCU : SPI slave 0x40004000, DMA ch0 ring, IRQ 30, framing  u16 len | u16 cmd | payload
 *   MCU -> module : UART2 0x40007000, 3 Mbaud 8N1,          framing  u16 len | u16 cmd | u8 status | extra
 * The packet length is bounds-checked. */
#include "link.h"
#include "../hal/regs.h"
#include "../hal/gpio.h"
#include "../hal/dma.h"
#include "../hal/irq.h"
#include "../hal/uart.h"

#define SPIS_CTRL   REG8(0x40004000u)
#define SPIS_STAT   REG32(0x40004004u)
#define SPIS_CMD    REG8(0x40004008u)
#define SPIS_RXDATA 0x40004010u
#define SPIS_IRQ    30u
#define SPIS_DMA_CH 0u
#define LINK_UART   2u
#define LINK_BAUD   3000000u

#define RING_SIZE   0x3080u

static uint8_t  ring[RING_SIZE] __attribute__((aligned(4)));
static uint8_t  pkt[LINK_MAX_PACKET] __attribute__((aligned(4)));
static uint16_t pkt_off;
static uint16_t pkt_expected;
static volatile uint8_t rx_active;

static void spis_isr(unsigned irq)
{
    (void)irq;
    if (SPIS_STAT & (1u << 5)) {
        SPIS_CTRL &= (uint8_t)~0x10u;     /* mask until the packet is consumed */
        SPIS_CMD  |= 0x08u;               /* ack */
        rx_active = 1;
    }
}

static void spis_rx_reset(void)
{
    SPIS_CMD |= 0x04u;
    pkt_expected = LINK_MAX_PACKET;
    rx_active = 0;
    pkt_off = 0;
    SPIS_CTRL |= 0x10u;
}

void link_init(void)
{
    /* UART2 reply channel: pin-mux sel 0 (P0.2/P0.3), 3 Mbaud */
    uart_pinmux(LINK_UART, 0);
    uart_init(LINK_UART, LINK_BAUD);

    /* SPI slave on P1.26 (CS), P1.27-29; mode 2 */
    REG32(UART_PINMUX) = (REG32(UART_PINMUX) & ~0x70u) | 0x50u;
    SPIS_CTRL = (uint8_t)((SPIS_CTRL & 0xFBu) | 0x02u);
    SPIS_CTRL = (uint8_t)((SPIS_CTRL & 0xFEu) | 0x88u);

    dma_ring_rx_setup(SPIS_DMA_CH, 0, SPIS_RXDATA, ring, RING_SIZE);
    SPIS_CTRL |= 0x40u;                   /* DMA enable */

    pkt_expected = LINK_MAX_PACKET;
    irq_register(SPIS_IRQ, spis_isr);
    SPIS_CTRL |= 0x10u;                   /* IRQ enable */
    irq_enable(SPIS_IRQ);
}

/* Returns packet length (>=4) when a full packet is available in *out, else 0. Call from the main loop. */
unsigned link_poll(const uint8_t **out)
{
    if (!gpio_read(GPIO_P1, 26)) return 0;            /* CS low: transfer in progress */
    if (!rx_active) return 0;

    unsigned n = dma_ring_available(SPIS_DMA_CH, RING_SIZE);
    if (n == 0) { spis_rx_reset(); return 0; }
    if (n > (unsigned)(LINK_MAX_PACKET - pkt_off)) n = LINK_MAX_PACKET - pkt_off;
    n = dma_ring_read(SPIS_DMA_CH, RING_SIZE, pkt + pkt_off, n);
    pkt_off = (uint16_t)(pkt_off + n);

    if (pkt_off < 2) return 0;
    if (pkt_off - n < 2) {
        pkt_expected = (uint16_t)(pkt[0] | (pkt[1] << 8));
        if (pkt_expected < 4 || pkt_expected > LINK_MAX_PACKET) { spis_rx_reset(); return 0; }   /* resync */
    }
    if (pkt_off == pkt_expected) {
        unsigned len = pkt_off;
        spis_rx_reset();
        *out = pkt;
        return len;
    }
    if (pkt_off > pkt_expected) spis_rx_reset();
    return 0;
}

void link_reply(uint16_t cmd, uint8_t status, const uint8_t *extra, unsigned extra_len)
{
    uint8_t hdr[5];
    uint16_t len = (uint16_t)(5u + extra_len);
    hdr[0] = (uint8_t)len; hdr[1] = (uint8_t)(len >> 8);
    hdr[2] = (uint8_t)cmd; hdr[3] = (uint8_t)(cmd >> 8);
    hdr[4] = status;
    uart_write(LINK_UART, hdr, sizeof hdr);
    if (extra_len) uart_write(LINK_UART, extra, extra_len);
}
