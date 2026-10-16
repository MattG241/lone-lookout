/*
 * world_int.h -- internal glue between the world modules.
 */
#ifndef LL_WORLD_INT_H
#define LL_WORLD_INT_H

#include "world/world.h"

void walk_init(void);                  /* walk.c: structures, walls, colliders */
void places_init(void);                /* places.c: interactables and sit spots */

#endif
