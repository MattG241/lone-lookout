/*
 * terrain.h -- chunked ground and grass tufts.
 */
#ifndef LL_TERRAIN_H
#define LL_TERRAIN_H

#include <stdbool.h>
#include "game/daycycle.h"

void terrain_build(float season);
/* Recolour for a new season (0 summer .. 1 autumn). Call with the GPU idle. */
void terrain_set_season(float season);
int terrain_draw(const daylight *d);           /* returns chunks drawn */
void terrain_draw_tufts(const daylight *d);
/* Is the 64 m chunk containing (x, z) within range of the eye? */
bool terrain_chunk_near(float x, float z, float range);

#endif
