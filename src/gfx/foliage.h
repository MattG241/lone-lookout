/*
 * foliage.h -- cut-paper trees with wind sway near the camera.
 */
#ifndef LL_FOLIAGE_H
#define LL_FOLIAGE_H

#include "game/daycycle.h"

void foliage_build(float season);
void foliage_set_season(float season);       /* GPU must be idle */
void foliage_update(float dt);
int foliage_draw(const daylight *d, float wind);   /* returns trees drawn */

#endif
