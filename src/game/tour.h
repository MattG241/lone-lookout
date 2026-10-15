/*
 * tour.h -- deterministic screenshot tour for documentation captures.
 */
#ifndef LL_TOUR_H
#define LL_TOUR_H

#include <stdbool.h>

/* Advance the tour one frame. Returns true when this frame should be black
 * (a separator between shots). Always false in the normal build. */
bool tour_frame(void);

#endif
