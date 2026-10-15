/*
 * terrain.h -- chunked ground and grass tufts.
 */
#ifndef LL_TERRAIN_H
#define LL_TERRAIN_H

#include "game/daycycle.h"

void terrain_build(void);
void terrain_draw(const daylight *d);
void terrain_draw_tufts(const daylight *d);

#endif
