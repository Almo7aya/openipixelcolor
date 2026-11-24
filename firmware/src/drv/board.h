#pragma once

#define BOARD_BUSY_PIN   30   /* P0.30 */
#define BOARD_P2_11_PIN  11   /* P2.11 */
#define BOARD_KEY_PIN     6   /* P2.6, active low */

void board_gpio_init(void);
void board_p2_11_set(int on);
void board_busy(int on);
