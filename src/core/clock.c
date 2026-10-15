/*
 * clock.c -- timebase-backed clock and fixed-step accumulator.
 */
#include <ogc/lwp_watchdog.h>
#include "core/clock.h"

static u64 s_start;
static double s_last;
static double s_acc;

void clock_init(void)
{
	s_start = gettime();
	s_last = 0.0;
	s_acc = 0.0;
}

double clock_now(void)
{
	return (double)ticks_to_microsecs(gettime() - s_start) * 1e-6;
}

int clock_steps(void)
{
#ifdef LL_SHOT_TOUR
	/* Screenshot tour: exactly one step per presented frame, so captures are
	 * deterministic no matter how fast the emulator runs. */
	return 1;
#else
	double now = clock_now();
	double dt = now - s_last;
	s_last = now;
	if (dt > 0.25) dt = 0.25;           /* e.g. after a long load or HOME menu */
	s_acc += dt;
	int steps = 0;
	while (s_acc >= SIM_DT && steps < 8) {
		s_acc -= SIM_DT;
		steps++;
	}
	return steps;
#endif
}
