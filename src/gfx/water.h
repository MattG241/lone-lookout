/*
 * water.h -- the tarn surface.
 */
#ifndef LL_WATER_H
#define LL_WATER_H

#include "game/daycycle.h"

void water_build(void);
void water_update(float dt);
void water_draw(const daylight *d);

#endif
