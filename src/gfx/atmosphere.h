#ifndef LL_ATMOSPHERE_H
#define LL_ATMOSPHERE_H
#include <stdbool.h>
#include "game/daycycle.h"
void atmosphere_update(float dt);
void atmosphere_draw(const daylight *d, bool indoors);
#endif
