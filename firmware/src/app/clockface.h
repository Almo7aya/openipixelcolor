#pragma once
#include <stdint.h>

typedef struct { uint8_t h, m, s; uint16_t year; uint8_t mon, day, dow; uint8_t synced; } clock_tm_t;

/* 0 digital+date, 1 analog, 2 binary, 3 big stacked, 4 digits over plasma,
   5 flip cards, 6 nixie tubes, 7 rings, 8 day/night sky, 9 pong clock */
#define CLOCK_STYLES 10

void clock_set_time(uint8_t h, uint8_t m, uint8_t s, uint32_t now_ms);
void clock_set_date(uint8_t yy, uint8_t mon, uint8_t day, uint8_t dow);   /* yy = 0..99, dow 1=Mon..7 */
void clock_now(uint32_t now_ms, clock_tm_t *tm);
void clock_draw(unsigned style, uint32_t now_ms, int fmt24, int show_date);
