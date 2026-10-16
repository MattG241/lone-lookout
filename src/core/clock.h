/*
 * clock.h -- wall clock and the fixed-step simulation accumulator.
 *
 * The game simulates at a fixed 60 Hz regardless of whether the renderer is
 * presenting at 60 or 30 fps (or 50/25 on PAL), so gameplay never speeds up
 * or slows down with frame pacing.
 */
#ifndef LL_CLOCK_H
#define LL_CLOCK_H

#include <gctypes.h>

#define SIM_HZ 60
#define SIM_DT (1.0f / SIM_HZ)

void clock_init(void);
double clock_now(void);            /* seconds since clock_init */

/* Returns how many fixed steps to run this frame (clamped to avoid spirals). */
int clock_steps(void);

#endif
