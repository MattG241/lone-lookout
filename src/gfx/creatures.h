/*
 * creatures.h -- the doe and fawns, and the Day 5 smoke column.
 */
#ifndef LL_CREATURES_H
#define LL_CREATURES_H

#include "game/daycycle.h"

void creatures_build(void);
void creatures_draw(const daylight *d);   /* with the props, fog on */
void smoke_draw(const daylight *d);       /* after the backdrop, before the terrain */

#endif
