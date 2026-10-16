/*
 * sites.h -- where the hand-placed things are: props, sit spots and notes
 * around each area. Positions are map metres (x east, z south). Facings
 * are compass bearings in radians (0 = north, pi/2 = east), the same
 * convention as the player's yaw.
 *
 * Shared by the collision code (world/walk.c), the interactables
 * (world/places.c) and the prop builders (gfx/props_*.c), so the thing you
 * see, the thing you bump into and the thing you can use always agree.
 */
#ifndef LL_SITES_H
#define LL_SITES_H

#include "world_data.h"

#define BEARING(deg) ((deg) * 0.0174532925f)

/* Cinder Ridge Overlook: Augie's carved bench, facing the sunset over the bowl. */
#define BENCH_X 403.0f
#define BENCH_Z 119.5f
#define BENCH_FACING BEARING(242.0f)
#define CAIRN_X 402.0f
#define CAIRN_Z 106.0f

/* Larkspur Meadow: the old drift fence and a fallen log facing the knob. */
#define FENCE_AX 126.0f
#define FENCE_AZ 314.0f
#define FENCE_BX 160.0f
#define FENCE_BZ 302.0f
#define FENCE_POSTS 9
#define FENCE_NOTE_POST 4          /* N05 is pinned to this post */
#define LOG_X 101.0f
#define LOG_Z 341.0f
#define LOG_FACING BEARING(52.0f)
#define GAUGE_X 113.0f             /* Augie's rain gauge, on a post by the log */
#define GAUGE_Z 334.0f

/* The cold spring: a pipe out of the bank, a trough, a tin cup on a nail. */
#define SPRING_X 196.0f
#define SPRING_Z 213.0f
#define SPRING_FACING BEARING(180.0f)

/* Mirror Tarn: the dock runs west from the east shore (world.h: DOCK_*). */

/* The '98 Burn: a tagged snag by the trail and a fallen snag to sit on. */
#define TAG_SNAG_X 385.0f
#define TAG_SNAG_Z 366.0f
#define FALLEN_X 401.0f
#define FALLEN_Z 381.0f
#define FALLEN_FACING BEARING(300.0f)

/* Pell's Cabin (CABIN_X/Z in world_data.h); the door and porch face south. */
#define CABIN_FACING BEARING(180.0f)

/* Trailhead: bear-proof supply box, a sign, and the road gate (GATE_X/Z). */
#define SUPPLY_X 241.5f
#define SUPPLY_Z 496.0f
#define SIGN_X 258.0f
#define SIGN_Z 493.0f
#define GATE_HALF 16.0f

/* Where Day 1 begins: at the trailhead, facing up the trail. */
#define START_X 250.0f
#define START_Z 497.0f
#define START_YAW BEARING(355.0f)

#endif
