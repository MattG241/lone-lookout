/*
 * daycycle.h -- in-game clock and the evaluated lighting for "now".
 *
 * The colour script lives in data/palette/time_of_day.json (compiled to
 * gen/palette_data.h). daycycle_eval() interpolates it and derives the sun,
 * moon and key-light directions every frame.
 */
#ifndef LL_DAYCYCLE_H
#define LL_DAYCYCLE_H

#include "core/mathx.h"

#define DAY_SECONDS_PER_HOUR 120.0f  /* 1 in-game hour = 2 real minutes (DESIGN.md section 3) */
#define DAY_TIMELAPSE_RATE   40.0f   /* while seated and holding Z: an hour in 3 s */

typedef struct {
	float hour;                      /* 0..24 */
	rgbf sky_top, sky_horizon, fog, sun, shadow, ambient;
	float key_strength;
	float fog_density;
	float window_glow;
	v3 sun_dir;                      /* unit vector toward the sun (may be below horizon) */
	v3 moon_dir;
	v3 key_dir;                      /* toward whichever light is lighting the world */
	float sun_vis;                   /* 0..1: sun disc above the horizon */
	float night;                     /* 0..1: star / moon visibility */
} daylight;

typedef struct {
	float hour;
	float rate;                      /* multiplier on real time (1 or timelapse) */
} dayclock;

extern dayclock DAY;

void daycycle_init(float start_hour);
void daycycle_update(float dt);
void daycycle_eval(daylight *out);

#endif
