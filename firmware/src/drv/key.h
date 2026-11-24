#pragma once
#include <stdint.h>

typedef enum { KEY_NONE = 0, KEY_SHORT = 1, KEY_LONG = 2 } key_event_t;

void        key_init(void);
void        key_tick_1ms(void);   /* call from the 1 ms tick ISR */
key_event_t key_poll(void);       /* call from the main loop */
int         key_is_down(void);
