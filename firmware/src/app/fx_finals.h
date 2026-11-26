/* Effects in fx_finals.c (fan-made THE FINALS animations; listed in the effects[] table in effects.c). */
#pragma once
#include <stdint.h>

void fin_intro_init(void);  void fin_intro_frame(uint32_t t);
void fin_live_init(void);   void fin_live_frame(uint32_t t);
void fin_cash_init(void);   void fin_cash_frame(uint32_t t);
void fin_spin_init(void);   void fin_spin_frame(uint32_t t);
void fin_em_init(void);
void fin_chrome_frame(uint32_t t);
void fin_crt_frame(uint32_t t);
void fin_neon_frame(uint32_t t);
void fin_shatter_frame(uint32_t t);
