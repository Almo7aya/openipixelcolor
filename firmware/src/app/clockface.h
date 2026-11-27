#pragma once
#include <stdint.h>

typedef struct { uint8_t h, m, s; uint16_t year; uint8_t mon, day, dow; uint8_t synced; } clock_tm_t;

/* 0 digital+date, 1 analog, 2 binary, 3 big stacked, 4 digits over plasma,
   5 flip cards, 6 nixie tubes, 7 rings, 8 day/night sky, 9 pong clock;
   clockface2.c: 10 words, 11 Berlin clock, 12 Fibonacci, 13 morphing digits, 14 hourglass, 15 falling bricks,
   16 matrix digits, 17 fire digits, 18 LCD watch, 19 neon sign, 20 dice, 21 moon phase, 22 orbit,
   23 progress, 24 colour code, 25 Roman numerals;
   clockface3.c: 26 falling blocks, 27 count the squares, 28 abacus, 29 oscilloscope, 30 clock of clocks,
   31 station clock, 32 cuckoo clock, 33 VFD, 34 flip-dot, 35 terminal, 36 odometer, 37 candle,
   38 decimal time, 39 radar, 40 ticker, 41 melting clock, 42 3D digits */
#define CLOCK_STYLES 43

void clock_set_time(uint8_t h, uint8_t m, uint8_t s, uint32_t now_ms);
void clock_set_date(uint8_t yy, uint8_t mon, uint8_t day, uint8_t dow);   /* yy = 0..99, dow 1=Mon..7 */
void clock_now(uint32_t now_ms, clock_tm_t *tm);
void clock_draw(unsigned style, uint32_t now_ms, int fmt24, int show_date);
