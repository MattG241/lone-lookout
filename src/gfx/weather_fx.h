/*
 * weather_fx.h -- rain streaks around the camera.
 */
#ifndef LL_WEATHER_FX_H
#define LL_WEATHER_FX_H

#include <stdbool.h>
#include "game/daycycle.h"

void rain_draw(const daylight *d, float rain, float wind, bool indoor_eye);

#endif
