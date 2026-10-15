/*
 * props_int.h -- shared by the prop builders (props_tower.c, props_places.c)
 * and the prop renderer (props.c).
 *
 * Every hand-built thing in the world is recorded into one of a few display
 * lists ("groups"), each with a bounding sphere for culling. Interiors are
 * separate groups so they can be drawn with the lamp-light material.
 */
#ifndef LL_PROPS_INT_H
#define LL_PROPS_INT_H

#include "gfx/mesh.h"
#include "world/world.h"
#include "world/sites.h"
#include "palette_data.h"

typedef enum {
	PG_TOWER, PG_TOWER_IN, PG_CABIN, PG_CABIN_IN, PG_TARN, PG_RIDGE, PG_MEADOW, PG_SPRING,
	PG_BURN, PG_TRAILHEAD, PG_BRIDGES, PG_BINOCULARS,
	PG_SHUTTERS_OPEN, PG_SHUTTERS_SHUT, PG_CAIRN_STONE,          /* drawn by story state */
	PG_COUNT
} prop_group;

void pg_begin(prop_group g, int boxes);          /* capacity in MESH_BOX_BYTES units */
void pg_end(prop_group g, v3 centre, float radius);

void build_tower_props(void);
void build_place_props(void);

static inline u32 col3(float r, float g, float b) { return rgba_u32(rgb_make(r, g, b), 1.0f); }
static inline u32 colp(const float *p, float k) { return rgba_u32(rgb_scale(rgb_from(p), k), 1.0f); }

/* Common materials (albedo). */
#define C_PAPER col3(0.93f, 0.90f, 0.82f)
#define C_IRON col3(0.26f, 0.25f, 0.25f)
#define C_ROCK colp(ALB_ROCK, 1.0f)
#define C_WEATHERED col3(0.56f, 0.51f, 0.46f)
#define C_GREY_WOOD col3(0.62f, 0.60f, 0.57f)
#define C_BARK col3(0.42f, 0.34f, 0.27f)
#define C_CUT col3(0.72f, 0.60f, 0.44f)

#endif
