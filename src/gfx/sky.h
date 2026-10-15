/*
 * sky.h -- sky dome, sun/moon, stars and the painted backdrop ranges.
 */
#ifndef LL_SKY_H
#define LL_SKY_H

#include "game/daycycle.h"

void sky_init(void);
void sky_draw(const daylight *d, float cloud);        /* dome, stars, sun/moon glow */
void backdrop_draw(const daylight *d);   /* distant ridge rings ("theatre flats") */
/* The rings in two parts, far (1150 m) then near (820 and 560 m), so the Day 5
 * smoke (900 m out) can be drawn between them. */
void backdrop_draw_far(const daylight *d);
void backdrop_draw_near(const daylight *d);

#endif
