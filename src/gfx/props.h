/*
 * props.h -- the hand-built world: Kestrel Lookout (with its cab), Pell's
 * Cabin, the dock, Augie's bench, the drift fence, the spring, the burn's
 * tagged snag, the trailhead and the footbridges.
 */
#ifndef LL_PROPS_H
#define LL_PROPS_H

#include <stdbool.h>
#include "game/daycycle.h"

typedef struct {
	float lamp;                  /* 0..1: the cab lamp */
	bool binoculars_taken;       /* the hook by the door is empty */
} props_state;

void props_build(void);
void props_draw(const daylight *d, const props_state *st);

#endif
