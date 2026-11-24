/* Calibrate the flash driver's delay loop against the 1 ms tick (runs normally from flash; only the
 * measured loop itself lives in the cache window, so its speed matches what the driver sees).
 * Every wait here is bounded: if the tick does not advance, fall back to a deliberately LARGE value
 * (minimum flash waits become longer, never shorter). */
#include "iflash.h"
#include "../hal/timer.h"

#define FALLBACK_LOOPS_PER_MS 250000u   /* >= any plausible core clock: waits only get longer */

uint32_t iflash_calibrate(void)
{
    const uint32_t chunk = 1000u, window_ms = 50u;
    uint32_t t = millis(), n = 0;
    while (millis() == t && ++n < 50000000u) { }          /* align to a tick edge (bounded) */
    if (millis() == t) { iflash_loops_per_ms = FALLBACK_LOOPS_PER_MS; return 0; }

    uint32_t start = millis(), loops = 0;
    while ((uint32_t)(millis() - start) < window_ms) {
        iflash_delay_loops(chunk);
        loops += chunk;
        if (loops > window_ms * FALLBACK_LOOPS_PER_MS * 4u) {        /* tick stalled mid-way */
            iflash_loops_per_ms = FALLBACK_LOOPS_PER_MS;
            return 0;
        }
    }
    uint32_t per_ms = loops / window_ms;
    per_ms += per_ms / 4u;            /* measurement includes millis() overhead: +25 % margin */
    if (per_ms < 1000u) per_ms = 1000u;
    iflash_loops_per_ms = per_ms;
    return per_ms;
}
