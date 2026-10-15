/*
 * world.h -- what the game needs to know about the ground it stands on.
 *
 * The Ashen Peaks are baked offline (tools/worldgen/bake_world.py) into a
 * 129 x 129 heightmap at 4 m, vertex colours for late summer and early
 * autumn, a surface-type grid, and tree and tuft placements. Coordinates are
 * map metres: x east, z south, origin at the NW corner; the playable square
 * is 512 x 512 m. Constants shared with the baker come from world_data.h.
 *
 * Buildings (the tower's stairs, deck and cab, Pell's Cabin, the dock) are
 * "walk patches": flat or sloped rectangles above the terrain, with walls
 * and colliders. world_ground() and world_move() combine them with the
 * terrain, the 38-degree slope limit and the district boundary.
 */
#ifndef LL_WORLD_H
#define LL_WORLD_H

#include <gctypes.h>
#include <stdbool.h>
#include "core/mathx.h"
#include "world_data.h"

/* --- Structures (tower-local u = x - TOWER_X, v = z - TOWER_Z) ----------- */
#define TOWER_DECK_Y 9.0f        /* catwalk height above the pad */
#define TOWER_DECK_HALF 3.0f     /* catwalk spans +/- this around the centre */
#define TOWER_CAB_HALF 2.0f      /* glazed cab footprint */
#define TOWER_CAB_H 2.5f         /* cab wall height */
#define TOWER_DOOR_HALF 0.55f    /* cab door, centred in the south wall */
#define TOWER_LEG_BASE 2.7f      /* leg spread at the ground */
#define TOWER_LEG_TOP 2.0f       /* leg spread under the deck */
#define STAIR_IN 3.0f            /* the stairs wind round the tower in a band */
#define STAIR_OUT 4.0f           /* between these offsets from the centre */
#define STAIR_RISE 2.25f         /* each of the four flights */
#define STAIR_STEPS 12           /* treads per flight */
#define STAIR_TOP_V 2.0f         /* flight D ends at v = 2; the top landing runs on to v = 4 */

#define CABIN_HALF_X 2.5f        /* Pell's Cabin: 5 x 4 m, door and porch on the south */
#define CABIN_HALF_Z 2.0f
#define CABIN_FLOOR_LIFT 0.35f
#define CABIN_WALL_H 2.3f
#define CABIN_PORCH_D 1.6f
#define CABIN_DOOR_HALF 0.55f

typedef enum {
	SURF_GRASS, SURF_DIRT, SURF_ROCK, SURF_WOOD, SURF_STAIR, SURF_NEEDLES, SURF_ASH, SURF_SCREE,
	SURF_COUNT
} surface_t;

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

typedef enum {
	TUFT_GRASS, TUFT_FLOWERS, TUFT_ROCK, TUFT_FERN, TUFT_SAPLING, TUFT_FIREWEED, TUFT_SEDGE,
	TUFT_KINDS
} tuft_kind;

typedef struct {
	float x, y, z, yaw, size;
	float shade;                 /* 0..1 colour variation */
	u8 kind;
} tuft_t;

/* --- Places ------------------------------------------------------------ */
typedef enum { AREA_TOWER, AREA_MEADOW, AREA_TARN, AREA_RIDGE, AREA_BURN, AREA_CABIN, AREA_COUNT } world_area;

typedef struct {
	const char *id, *name;
	float x, z, radius;
} world_place;

const world_place *world_areas(void);                   /* AREA_COUNT entries */
const world_place *world_landmarks(int *count);
int world_area_at(float x, float z);                    /* -1 when between areas */

typedef enum { INDOOR_NONE, INDOOR_CAB, INDOOR_CABIN } indoor_t;
indoor_t world_indoor(v3 feet);

/* --- Interactables ------------------------------------------------------ */
typedef enum {
	IA_NOTE,          /* read a note (id = note number) */
	IA_SIT,           /* sit spot */
	IA_SLEEP,         /* the cot */
	IA_LAMP,
	IA_KETTLE,
	IA_LOGBOOK,
	IA_FINDER,        /* the fire finder */
	IA_HANDSET,       /* the radio on the cab table */
	IA_BINOCULARS,    /* the hook by the cab door */
	IA_SUPPLY,        /* the bear-proof box at the trailhead */
	IA_CHIMES,        /* spoon chimes on the cabin porch */
	IA_GATE,          /* the road gate (the way out, at the end) */
	IA_WEATHER,       /* the instrument shelter on the catwalk */
} ia_kind;

typedef struct {
	v3 pos;                      /* point the reticle aims at */
	float radius;                /* aim tolerance in metres */
	ia_kind kind;
	const char *verb;            /* one-word handwritten prompt */
	int id;
	v3 sit_eye;                  /* IA_SIT: camera position when seated */
	float sit_yaw;               /* IA_SIT: facing when seated */
} interactable_t;

/* Note ids (DESIGN.md section 7). */
enum {
	NOTE_WELCOME, NOTE_STAIR, NOTE_MAP, NOTE_BIRDS, NOTE_FENCE, NOTE_SPRING, NOTE_DOCK,
	NOTE_SNAG, NOTE_BENCH, NOTE_FLOWERS, NOTE_LETTER, NOTE_POSTCARD, NOTE_COUNT
};

/* --- Terrain and movement ----------------------------------------------- */
void world_init(void);

float world_height(float x, float z);              /* terrain surface (mesh-exact) */
v3 world_normal(float x, float z);                 /* of the terrain triangle under (x, z) */
surface_t world_surface(float x, float z);         /* terrain ground type (trails are dirt) */

/* Highest walkable surface at (x, z) not more than a step above y_ref. */
float world_ground(float x, float z, float y_ref, surface_t *surf);
/* Resolve horizontal movement from 'from' to 'to' for a player whose feet are
 * at feet_y. Returns the corrected position (y untouched). If 'edge' is not
 * NULL it receives a boundary id (WORLD_EDGE_*) when the district line
 * stopped the move. */
enum { WORLD_EDGE_NONE, WORLD_EDGE_RIM, WORLD_EDGE_GATE, WORLD_EDGE_GORGE, WORLD_EDGE_WATER };
v3 world_move(v3 from, v3 to, float feet_y, int *edge);
/* Which stair tread the feet are on: flight * STAIR_STEPS + step, or -1. */
int world_stair_tread(v3 feet);

/* Grid access for the renderer. */
float world_grid_height(int i, int j);
v3 world_grid_normal(int i, int j);
u32 world_grid_color(int i, int j, float season);  /* RGBA8, season 0 summer .. 1 autumn */

/* Trails and the creek, as polylines. */
int world_trail_count(void);
const float (*world_trail(int t, int *count))[2];
float world_trail_dist(float x, float z);
const float (*world_creek(int *count))[2];
int world_bridges(const float (**out)[3]);          /* x, z, yaw of each footbridge */

/* Placement helpers for the prop builders. */
float world_pad_height(void);                       /* tower pad surface */
float world_cabin_floor(void);                      /* Pell's Cabin floor height */
float world_dock_x0(void);                          /* dock: shore end x (z = DOCK_Z) */
#define DOCK_Z 150.0f
#define DOCK_LEN 9.0f
#define DOCK_HALF_W 0.8f
#define DOCK_DECK_Y (TARN_WATER_Y + 0.45f)

const tree_t *world_trees(int *count);
const tuft_t *world_tufts(int *count);
const interactable_t *world_interactables(int *count);
int world_find_interactable(ia_kind kind, int id);  /* index, or -1 */

#endif
