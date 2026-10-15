/*
 * foliage.h -- cut-paper trees with wind sway.
 */
#ifndef LL_FOLIAGE_H
#define LL_FOLIAGE_H

#include "game/daycycle.h"

void foliage_build(void);
void foliage_update(float dt);
void foliage_draw(const daylight *d, float wind);

#endif
