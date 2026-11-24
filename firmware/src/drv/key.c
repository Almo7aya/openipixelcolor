/* Single key on P2.6 (active low, external pull-up). 32 ms debounce; events are decided on
 * release by how long the key was held: < 700 ms = KEY_SHORT, otherwise KEY_LONG. */
#include "key.h"
#include "board.h"
#include "../hal/gpio.h"
#include "../hal/timer.h"

#define DEBOUNCE_MS 32u
#define LONG_MS     700u

static volatile uint16_t debounce_ms;
static uint8_t  stable = 1, prev = 1;
static uint32_t press_t;

void key_init(void)
{
    gpio_input(GPIO_P2, BOARD_KEY_PIN);
    stable = prev = (uint8_t)gpio_read(GPIO_P2, BOARD_KEY_PIN);
}

void key_tick_1ms(void)
{
    if (debounce_ms) debounce_ms--;
}

key_event_t key_poll(void)
{
    key_event_t ev = KEY_NONE;
    uint8_t raw = (uint8_t)gpio_read(GPIO_P2, BOARD_KEY_PIN);      /* 1 = released */
    if (raw != stable) {
        if (raw != prev) {
            debounce_ms = DEBOUNCE_MS;
        } else if (debounce_ms == 0) {
            if (stable && !raw) press_t = millis();                 /* pressed */
            if (!stable && raw) ev = (millis() - press_t) < LONG_MS ? KEY_SHORT : KEY_LONG;
            stable = raw;
        }
    }
    prev = raw;
    return ev;
}

int key_is_down(void) { return stable == 0; }
