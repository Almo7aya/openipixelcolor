/* Board-level pins, set up the way the vendor firmware leaves them. */
#include "board.h"
#include "../hal/gpio.h"

void board_gpio_init(void)
{
    /* P1.30 / P1.31: only the pull config is set (reg9 clear, reg10 set) - role unknown */
    gpio_clr_bits(GPIO_P1, GPIO_R_PULL9,  (1u << 30) | (1u << 31));
    gpio_set_bits(GPIO_P1, GPIO_R_PULL10, (1u << 30) | (1u << 31));

    /* P1.8..P1.12: display bus outputs (driven by the display driver) */
    for (unsigned pin = 8; pin <= 12; pin++) {
        gpio_set_bits(GPIO_P1, GPIO_R_OE, 1u << pin);
        gpio_clr_bits(GPIO_P1, GPIO_R_IE, 1u << pin);
    }

    /* P0.30: "busy" output (high while decoding/rendering in the vendor firmware) */
    gpio_clr_bits(GPIO_P0, GPIO_R_IE, 1u << BOARD_BUSY_PIN);
    gpio_set_bits(GPIO_P0, GPIO_R_OE, 1u << BOARD_BUSY_PIN);

    /* P2.11: output, low during init; driven high after init (power enable?) */
    gpio_clr_bits(GPIO_P2, GPIO_R_IE, 1u << BOARD_P2_11_PIN);
    gpio_set_bits(GPIO_P2, GPIO_R_OE, 1u << BOARD_P2_11_PIN);
    gpio_clr_bits(GPIO_P2, GPIO_R_OUT, 1u << BOARD_P2_11_PIN);
}

void board_p2_11_set(int on)  { gpio_write(GPIO_P2, BOARD_P2_11_PIN, on); }
void board_busy(int on)       { gpio_write(GPIO_P0, BOARD_BUSY_PIN, on); }
