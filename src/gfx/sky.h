/*
 * sky.h -- sky dome, sun/moon, stars and the painted backdrop ranges.
 */
#ifndef LL_SKY_H
#define LL_SKY_H

#include "game/daycycle.h"

void sky_init(void);
void sky_draw(const daylight *d);        /* dome, stars, sun/moon glow */
void backdrop_draw(const daylight *d);   /* distant ridge rings ("theatre flats") */

#endif
