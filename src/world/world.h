/*
 * world.h -- what the game needs to know about the ground it stands on.
 *
 * Phase 2 implements this with a 200 x 200 m test area (world/testarea.c):
 * a gentle bowl with the Kestrel tower blockout, a log bench, a trail and a
 * few dozen cut-paper trees. Phase 3 swaps in the baked Ashen Peaks map
 * behind the same interface.
 */
#ifndef LL_WORLD_H
#define LL_WORLD_H

#include <gctypes.h>
#include <stdbool.h>
#include "core/mathx.h"

#define WORLD_HALF 100.0f        /* terrain spans [-100, 100] m on x and z */
#define WORLD_CELL 2.5f          /* metres between height samples */
#define WORLD_VERTS 81           /* (2 * WORLD_HALF / WORLD_CELL) + 1 */
#define WORLD_WALK_RADIUS 72.0f  /* soft edge of the walkable area */

/* Tower blockout dimensions (shared by props.c and the collision code). */
#define TOWER_X 0.0f
#define TOWER_Z -10.0f
#define TOWER_DECK_Y 9.0f        /* catwalk height above the pad */
#define TOWER_DECK_HALF 3.0f     /* catwalk spans +/- this around the centre */
#define TOWER_CAB_HALF 2.0f      /* glazed cab footprint */
#define TOWER_LEG_BASE 2.7f      /* leg spread at the ground */
#define TOWER_LEG_TOP 2.0f       /* leg spread under the deck */
#define STAIR_X 0.0f             /* stair centre line (x) */
#define STAIR_HALF_W 0.55f
#define STAIR_RUN 11.5f          /* horizontal run of the single flight */

typedef enum { SURF_GRASS, SURF_DIRT, SURF_ROCK, SURF_WOOD, SURF_STAIR } surface_t;

typedef enum {
	TREE_CONIFER0, TREE_CONIFER1, TREE_CONIFER2, TREE_CONIFER3,
	TREE_ASPEN, TREE_SNAG, TREE_KINDS
} tree_kind;

typedef struct {
	float x, y, z;
	float yaw;
	float height, width;
	float tint;                  /* -1..1 colour variation */
	float phase;                 /* wind sway phase */
	u8 kind;
} tree_t;

typedef enum { TUFT_GRASS, TUFT_FLOWERS, TUFT_ROCK, TUFT_KINDS } tuft_kind;

typedef struct {
	float x, y, z, yaw, size;
	u8 kind;
} tuft_t;

typedef enum { IA_NOTE, IA_SIT } ia_kind;

typedef struct {
	v3 pos;                      /* point the reticle aims at */
	float radius;                /* aim tolerance in metres */
	ia_kind kind;
	const char *verb;            /* one-word handwritten prompt */
	int id;
	v3 sit_eye;                  /* IA_SIT: camera position when seated */
	float sit_yaw;               /* IA_SIT: facing when seated */
} interactable_t;

void world_init(void);

float world_height(float x, float z);              /* terrain surface only */
v3 world_normal(float x, float z);
void world_vertex_color(float x, float z, float h, v3 n, rgbf *albedo, float *veg);

/* Highest walkable surface at (x, z) not more than a step above y_ref. */
float world_ground(float x, float z, float y_ref, surface_t *surf);
/* Resolve horizontal movement from 'from' to 'to' for a player whose feet are
 * at feet_y. Returns the corrected position (y untouched). */
v3 world_move(v3 from, v3 to, float feet_y);

/* Placement helpers for the prop builder. */
float world_pad_height(void);                      /* tower pad surface */
v3 world_bench_pos(void);                          /* log bench centre on the ground */

const tree_t *world_trees(int *count);
const tuft_t *world_tufts(int *count);
const interactable_t *world_interactables(int *count);

#endif
