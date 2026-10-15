/*
 * days.h -- the seven days, their weather, and the season.
 *
 * DESIGN.md section 3: seven days across the last four weeks of the
 * season. Each has a date, a weather pattern and a "season" value (0 late
 * summer .. 1 early autumn) that recolours grass and aspens. The weather is
 * evaluated from the day and the hour, and folded into the colour script's
 * lighting before anything is drawn.
 */
#ifndef LL_DAYS_H
#define LL_DAYS_H

#include <stdbool.h>
#include "game/daycycle.h"

#define NUM_DAYS 7

typedef enum { WX_CLEAR, WX_WARM, WX_HIGH_CLOUD, WX_STORM, WX_HAZE, WX_COLD, WX_RAIN } weather_kind;

typedef struct {
	const char *title;           /* "Channel Four" */
	const char *date;            /* "21 August" */
	float season;
	weather_kind weather;
} day_def;

typedef struct {
	float cloud;                 /* 0 clear .. 1 overcast */
	float haze;                  /* smoke from far-off fires */
	float storm;                 /* 0..1 dry thunderstorm */
	float rain;                  /* 0..1 */
	float wind;                  /* 0..1.5 */
	float cold;                  /* crisp, clear air */
	float flash;                 /* lightning, 0..1 this frame */
} weather_t;

extern weather_t WX;

const day_def *day_info(int day);            /* 1-based */
void weather_update(int day, float hour, float dt);
void weather_apply(daylight *d);             /* fold the weather into the lighting */

#endif
