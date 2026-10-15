/*
 * walk.c -- where you can stand and where you can't.
 *
 * Structures are "walk patches": rectangles with a height plane (level for
 * floors, sloped for stair flights). The ground at a point is the highest of
 * the terrain and any patch within a step of your feet. Three general rules
 * then do the work of most railings:
 *   - you can't walk off a structure where the drop is more than a metre;
 *   - you can't walk into a structure between knee and head height;
 *   - on open ground, slopes steeper than 38 degrees can't be entered.
 * Walls (with door gaps), posts and boxes push the player out; the district
 * line is the bowl's rim, the deep tarn and the road gate.
 */
#include <math.h>
#include <stddef.h>
#include "world/world.h"
#include "world/world_int.h"
#include "world/sites.h"

#define STEP_UP 0.6f
#define DROP_OK 1.0f
#define SUPPORT_BELOW 0.7f
#define PLAYER_R 0.28f
#define SLOPE_COS 0.788f            /* cos(38 degrees) */
#define BODY_LO 0.05f
#define BODY_HI 1.7f

typedef struct {
	float cx, cz, hx, hz;            /* axis-aligned rectangle */
	float y0, gx, gz;                /* y = y0 + gx (x - cx) + gz (z - cz) */
	u8 surf;
	s8 flight;                       /* tower flight 0..3, else -1 */
	float y_bottom;                  /* flight: height at its foot */
} patch_t;

typedef struct {
	float ax, az, bx, bz;
	float y0, y1;                    /* blocks bodies overlapping [y0, y1] */
	float r;
	u8 edge;                         /* WORLD_EDGE_* reported when it stops a move */
} wall_t;

typedef struct {
	float x, z, r, y0, y1;
} circle_t;

typedef struct {
	float cx, cz, hx, hz, c, s;      /* oriented box: half extents along (c, s) and (-s, c) */
	float y0, y1;
} box_t;

#define MAX_PATCHES 24
#define MAX_WALLS 40
#define MAX_CIRCLES 64
#define MAX_BOXES 32

static patch_t s_patch[MAX_PATCHES];
static int s_np;
static wall_t s_wall[MAX_WALLS];
static int s_nw;
static circle_t s_circ[MAX_CIRCLES];
static int s_nc;
static box_t s_box[MAX_BOXES];
static int s_nb;

static float s_pad_y, s_cabin_floor, s_dock_x0;

float world_pad_height(void) { return s_pad_y; }
float world_cabin_floor(void) { return s_cabin_floor; }
float world_dock_x0(void) { return s_dock_x0; }

/* ------------------------------------------------------------------------ */
/* Builders                                                                 */
/* ------------------------------------------------------------------------ */

static patch_t *add_patch(float x0, float z0, float x1, float z1, float y_at_centre, float gx, float gz, u8 surf)
{
	patch_t *p = &s_patch[s_np++];
	p->cx = (x0 + x1) * 0.5f;
	p->cz = (z0 + z1) * 0.5f;
	p->hx = fabsf(x1 - x0) * 0.5f;
	p->hz = fabsf(z1 - z0) * 0.5f;
	p->y0 = y_at_centre;
	p->gx = gx;
	p->gz = gz;
	p->surf = surf;
	p->flight = -1;
	return p;
}

static void add_wall(float ax, float az, float bx, float bz, float y0, float y1, float r, u8 edge)
{
	wall_t *w = &s_wall[s_nw++];
	*w = (wall_t){ ax, az, bx, bz, y0, y1, r, edge };
}

static void add_circle(float x, float z, float r, float y0, float y1)
{
	s_circ[s_nc++] = (circle_t){ x, z, r, y0, y1 };
}

static void add_box(float cx, float cz, float hx, float hz, float yaw, float y0, float y1)
{
	/* yaw is a bearing: hz runs along it, hx across it (a bench "faces" its
	 * bearing and is long across it). */
	box_t *b = &s_box[s_nb++];
	*b = (box_t){ cx, cz, hx, hz, cosf(yaw), sinf(yaw), y0, y1 };
}

static void build_tower(void)
{
	const float X = TOWER_X, Z = TOWER_Z, y0 = s_pad_y;
	const float I = STAIR_IN, O = STAIR_OUT;
	const float g = STAIR_RISE / (2.0f * STAIR_IN);          /* flights A-C run 6 m */
	const float deck = y0 + TOWER_DECK_Y;

	/* Deck and the top landing (west of the deck's SW corner). */
	add_patch(X - TOWER_DECK_HALF, Z - TOWER_DECK_HALF, X + TOWER_DECK_HALF, Z + TOWER_DECK_HALF, deck, 0, 0, SURF_WOOD);
	add_patch(X - O, Z + STAIR_TOP_V, X - I, Z + O, deck, 0, 0, SURF_WOOD);

	/* Flight A: south side, climbing east. */
	patch_t *p = add_patch(X - I, Z + I, X + I, Z + O, y0 + STAIR_RISE * 0.5f, g, 0, SURF_STAIR);
	p->flight = 0;
	p->y_bottom = y0;
	add_patch(X + I, Z + I, X + O, Z + O, y0 + STAIR_RISE, 0, 0, SURF_WOOD);            /* SE landing */
	/* Flight B: east side, climbing north. */
	p = add_patch(X + I, Z - I, X + O, Z + I, y0 + STAIR_RISE * 1.5f, 0, -g, SURF_STAIR);
	p->flight = 1;
	p->y_bottom = y0 + STAIR_RISE;
	add_patch(X + I, Z - O, X + O, Z - I, y0 + STAIR_RISE * 2.0f, 0, 0, SURF_WOOD);     /* NE landing */
	/* Flight C: north side, climbing west. */
	p = add_patch(X - I, Z - O, X + I, Z - I, y0 + STAIR_RISE * 2.5f, -g, 0, SURF_STAIR);
	p->flight = 2;
	p->y_bottom = y0 + STAIR_RISE * 2.0f;
	add_patch(X - O, Z - O, X - I, Z - I, y0 + STAIR_RISE * 3.0f, 0, 0, SURF_WOOD);     /* NW landing */
	/* Flight D: west side, climbing south to the top landing (a 5 m run). */
	float gd = STAIR_RISE / (STAIR_TOP_V + STAIR_IN);
	p = add_patch(X - O, Z - I, X - I, Z + STAIR_TOP_V, y0 + STAIR_RISE * 3.5f, 0, gd, SURF_STAIR);
	p->flight = 3;
	p->y_bottom = y0 + STAIR_RISE * 3.0f;

	/* The deck's west railing, so flight D can't be left sideways onto the
	 * deck; the gap at the top landing is the way on. */
	add_wall(X - TOWER_DECK_HALF, Z - TOWER_DECK_HALF, X - TOWER_DECK_HALF, Z + STAIR_TOP_V, deck - 0.4f, deck + 1.0f, 0.04f, 0);

	/* Cab walls with the door in the south wall. */
	float c = TOWER_CAB_HALF, dh = TOWER_DOOR_HALF, top = deck + TOWER_CAB_H;
	add_wall(X - c, Z - c, X + c, Z - c, deck, top, 0.05f, 0);
	add_wall(X + c, Z - c, X + c, Z + c, deck, top, 0.05f, 0);
	add_wall(X - c, Z - c, X - c, Z + c, deck, top, 0.05f, 0);
	add_wall(X - c, Z + c, X - dh, Z + c, deck, top, 0.05f, 0);
	add_wall(X + dh, Z + c, X + c, Z + c, deck, top, 0.05f, 0);

	/* Cab furniture (positions match gfx/props_tower.c). */
	add_circle(X, Z, 0.36f, deck, deck + 1.2f);                                 /* fire finder */
	add_box(X - 1.55f, Z - 0.3f, 0.42f, 1.0f, 0.0f, deck, deck + 0.6f);         /* cot, west wall */
	add_circle(X + 1.45f, Z - 1.45f, 0.36f, deck, deck + 1.0f);                 /* stove, NE corner */
	add_box(X + 1.62f, Z + 0.35f, 0.34f, 0.6f, 0.0f, deck, deck + 0.8f);        /* radio table, east wall */

	/* Legs at ground level. */
	for (int k = 0; k < 4; k++) {
		float sx = (k & 1) ? 1.0f : -1.0f, sz = (k & 2) ? 1.0f : -1.0f;
		add_circle(X + sx * TOWER_LEG_BASE, Z + sz * TOWER_LEG_BASE, 0.2f, y0 - 2.0f, y0 + 4.0f);
	}
	/* Woodpile and cistern at the base. */
	add_box(X - 5.6f, Z - 1.0f, 0.5f, 1.3f, 0.0f, y0 - 1.0f, y0 + 1.2f);
	add_circle(X + 5.4f, Z - 4.6f, 0.75f, y0 - 1.0f, y0 + 1.6f);
}

static void build_cabin(void)
{
	const float X = CABIN_X, Z = CABIN_Z;
	const float hx = CABIN_HALF_X, hz = CABIN_HALF_Z;
	/* Floor sits a little above the highest ground under the cabin and porch. */
	float top = -1e9f;
	for (float z = Z - hz; z <= Z + hz + CABIN_PORCH_D + 0.01f; z += 0.5f)
		for (float x = X - hx; x <= X + hx + 0.01f; x += 0.5f)
			top = fmaxf(top, world_height(x, z));
	s_cabin_floor = top + CABIN_FLOOR_LIFT;
	float f = s_cabin_floor;

	add_patch(X - hx, Z - hz, X + hx, Z + hz, f, 0, 0, SURF_WOOD);
	add_patch(X - hx, Z + hz, X + hx, Z + hz + CABIN_PORCH_D, f - 0.03f, 0, 0, SURF_WOOD);

	float w0 = f - 0.6f, w1 = f + CABIN_WALL_H, dh = CABIN_DOOR_HALF;
	add_wall(X - hx, Z - hz, X + hx, Z - hz, w0, w1, 0.08f, 0);
	add_wall(X + hx, Z - hz, X + hx, Z + hz, w0, w1, 0.08f, 0);
	add_wall(X - hx, Z - hz, X - hx, Z + hz, w0, w1, 0.08f, 0);
	add_wall(X - hx, Z + hz, X - dh, Z + hz, w0, w1, 0.08f, 0);
	add_wall(X + dh, Z + hz, X + hx, Z + hz, w0, w1, 0.08f, 0);
	/* Porch posts. */
	add_circle(X - hx + 0.12f, Z + hz + CABIN_PORCH_D - 0.12f, 0.1f, f - 1.0f, f + 2.0f);
	add_circle(X + hx - 0.12f, Z + hz + CABIN_PORCH_D - 0.12f, 0.1f, f - 1.0f, f + 2.0f);
	/* Inside: Augie's table (NE) and a bunk along the west wall. */
	add_box(X + 1.5f, Z - 1.2f, 0.75f, 0.5f, 0.0f, f, f + 0.8f);
	add_box(X - 1.95f, Z - 0.4f, 0.42f, 1.0f, 0.0f, f, f + 0.6f);
}

static void build_sites(void)
{
	/* Dock: find the east shore along DOCK_Z, then run DOCK_LEN west. */
	float x = TARN_X + 60.0f;
	while (x > TARN_X && world_height(x, DOCK_Z) > TARN_WATER_Y + 0.05f) x -= 0.25f;
	s_dock_x0 = x + 1.6f;
	add_patch(s_dock_x0 - DOCK_LEN, DOCK_Z - DOCK_HALF_W, s_dock_x0, DOCK_Z + DOCK_HALF_W, DOCK_DECK_Y, 0, 0, SURF_WOOD);

	/* Ridge bench and cairn; meadow log; burn snags; spring trough. */
	float hb = world_height(BENCH_X, BENCH_Z);
	add_box(BENCH_X, BENCH_Z, 0.95f, 0.28f, BENCH_FACING, hb - 1.0f, hb + 0.9f);
	float hc = world_height(CAIRN_X, CAIRN_Z);
	add_circle(CAIRN_X, CAIRN_Z, 0.55f, hc - 1.0f, hc + 1.2f);
	float hl = world_height(LOG_X, LOG_Z);
	add_box(LOG_X, LOG_Z, 1.6f, 0.3f, LOG_FACING, hl - 1.0f, hl + 0.6f);
	float hf = world_height(FALLEN_X, FALLEN_Z);
	add_box(FALLEN_X, FALLEN_Z, 2.6f, 0.3f, FALLEN_FACING, hf - 1.0f, hf + 0.6f);
	float hs = world_height(SPRING_X, SPRING_Z);
	add_box(SPRING_X, SPRING_Z - 0.6f, 0.7f, 0.35f, 0.0f, hs - 1.0f, hs + 0.7f);
	/* Drift fence: posts only (the rails are down in places; step over them). */
	for (int k = 0; k < FENCE_POSTS; k++) {
		float t = (float)k / (FENCE_POSTS - 1);
		float px = lerpf(FENCE_AX, FENCE_BX, t), pz = lerpf(FENCE_AZ, FENCE_BZ, t);
		float h = world_height(px, pz);
		add_circle(px, pz, 0.1f, h - 1.0f, h + 1.3f);
	}
	/* Trailhead: supply box, sign, and the gate across the road. */
	float hsb = world_height(SUPPLY_X, SUPPLY_Z);
	add_box(SUPPLY_X, SUPPLY_Z, 0.6f, 0.4f, BEARING(90.0f), hsb - 1.0f, hsb + 1.0f);
	float hsg = world_height(SIGN_X, SIGN_Z);
	add_circle(SIGN_X, SIGN_Z, 0.12f, hsg - 1.0f, hsg + 2.0f);
	add_wall(GATE_X - GATE_HALF, GATE_Z, GATE_X + GATE_HALF, GATE_Z, -1000.0f, 1000.0f, 0.1f, WORLD_EDGE_GATE);
}

void walk_init(void)
{
	s_np = s_nw = s_nc = s_nb = 0;
	s_pad_y = world_height(TOWER_X, TOWER_Z);
	build_tower();
	build_cabin();
	build_sites();
}

/* ------------------------------------------------------------------------ */
/* Queries                                                                  */
/* ------------------------------------------------------------------------ */

static bool patch_at(const patch_t *p, float x, float z, float *y)
{
	if (fabsf(x - p->cx) > p->hx || fabsf(z - p->cz) > p->hz) return false;
	*y = p->y0 + p->gx * (x - p->cx) + p->gz * (z - p->cz);
	return true;
}

/* Highest patch at (x, z) whose surface lies in [lo, hi]; -1 if none. */
static int patch_in_range(float x, float z, float lo, float hi, float *y_out)
{
	int best = -1;
	float best_y = -1e9f, y;
	for (int i = 0; i < s_np; i++)
		if (patch_at(&s_patch[i], x, z, &y) && y >= lo && y <= hi && y > best_y) {
			best_y = y;
			best = i;
		}
	if (best >= 0 && y_out) *y_out = best_y;
	return best;
}

float world_ground(float x, float z, float y_ref, surface_t *surf)
{
	float best = world_height(x, z), y;
	surface_t s = world_surface(x, z);
	int p = patch_in_range(x, z, best, y_ref + STEP_UP, &y);
	if (p >= 0) {
		best = y;
		s = (surface_t)s_patch[p].surf;
	}
	if (surf) *surf = s;
	return best;
}

int world_stair_tread(v3 feet)
{
	float y;
	for (int i = 0; i < s_np; i++) {
		const patch_t *p = &s_patch[i];
		if (p->flight < 0 || !patch_at(p, feet.x, feet.z, &y) || fabsf(y - feet.y) > 0.3f) continue;
		int step = (int)((feet.y - p->y_bottom) / STAIR_RISE * STAIR_STEPS);
		step = step < 0 ? 0 : (step >= STAIR_STEPS ? STAIR_STEPS - 1 : step);
		return p->flight * STAIR_STEPS + step;
	}
	return -1;
}

indoor_t world_indoor(v3 f)
{
	if (fabsf(f.x - TOWER_X) < TOWER_CAB_HALF && fabsf(f.z - TOWER_Z) < TOWER_CAB_HALF &&
	    f.y > s_pad_y + TOWER_DECK_Y - 0.3f)
		return INDOOR_CAB;
	if (fabsf(f.x - CABIN_X) < CABIN_HALF_X && fabsf(f.z - CABIN_Z) < CABIN_HALF_Z && f.y > s_cabin_floor - 0.2f)
		return INDOOR_CABIN;
	return INDOOR_NONE;
}

static int blocked(v3 from, v3 to, float feet)
{
	if (to.x < 3.0f || to.x > WORLD_SIZE - 3.0f || to.z < 3.0f || to.z > WORLD_SIZE - 3.0f)
		return WORLD_EDGE_RIM;
	/* Never walk into a structure between knee and head height. */
	if (patch_in_range(to.x, to.z, feet + STEP_UP + 0.01f, feet + 1.9f, NULL) >= 0) return -1;
	/* Standing on a structure there: fine (walls are handled separately). */
	if (patch_in_range(to.x, to.z, feet - SUPPORT_BELOW, feet + STEP_UP, NULL) >= 0) return 0;

	float h_to = world_height(to.x, to.z), h_from = world_height(from.x, from.z);
	bool on_structure = feet > h_from + 0.3f;
	if (on_structure && (h_to < feet - DROP_OK || h_to > feet + STEP_UP)) return -1;

	/* The tarn: wade the margins, never the deep water. */
	float dx = to.x - TARN_X, dz = to.z - TARN_Z;
	if (dx * dx + dz * dz < TARN_RADIUS * TARN_RADIUS && h_to < TARN_WATER_Y - 0.25f)
		return WORLD_EDGE_WATER;

	/* Slopes over 38 degrees can't be entered, and can only be left downhill. */
	v3 n = world_normal(to.x, to.z);
	if (n.y < SLOPE_COS) {
		v3 nf = world_normal(from.x, from.z);
		if (nf.y >= SLOPE_COS || h_to > h_from) return WORLD_EDGE_RIM;
	}
	return 0;
}

static void push_circle(v3 *p, float cx, float cz, float r)
{
	float dx = p->x - cx, dz = p->z - cz;
	float d2 = dx * dx + dz * dz;
	if (d2 < r * r) {
		float d = sqrtf(d2);
		if (d < 1e-4f) {
			dx = 1.0f;
			dz = 0.0f;
			d = 1.0f;
		}
		p->x = cx + dx / d * r;
		p->z = cz + dz / d * r;
	}
}

static bool body_overlaps(float feet, float y0, float y1)
{
	return feet + BODY_HI > y0 && feet + BODY_LO < y1;
}

static int push_out(v3 *p, float feet)
{
	int edge = 0;
	for (int i = 0; i < s_nw; i++) {
		const wall_t *w = &s_wall[i];
		if (!body_overlaps(feet, w->y0, w->y1)) continue;
		float vx = w->bx - w->ax, vz = w->bz - w->az;
		float t = clampf(((p->x - w->ax) * vx + (p->z - w->az) * vz) / (vx * vx + vz * vz), 0.0f, 1.0f);
		float cx = w->ax + vx * t, cz = w->az + vz * t;
		float r = w->r + PLAYER_R;
		float dx = p->x - cx, dz = p->z - cz;
		if (dx * dx + dz * dz < r * r) {
			push_circle(p, cx, cz, r);
			if (w->edge) edge = w->edge;
		}
	}
	for (int i = 0; i < s_nc; i++) {
		const circle_t *c = &s_circ[i];
		if (fabsf(p->x - c->x) < c->r + PLAYER_R && fabsf(p->z - c->z) < c->r + PLAYER_R &&
		    body_overlaps(feet, c->y0, c->y1))
			push_circle(p, c->x, c->z, c->r + PLAYER_R);
	}
	for (int i = 0; i < s_nb; i++) {
		const box_t *b = &s_box[i];
		if (!body_overlaps(feet, b->y0, b->y1)) continue;
		/* Local frame: +z along the facing (c = cos, s = sin of the bearing). */
		float dx = p->x - b->cx, dz = p->z - b->cz;
		float lz = dx * b->s - dz * b->c;            /* along the bearing */
		float lx = dx * b->c + dz * b->s;            /* to its right */
		float ox = b->hx + PLAYER_R - fabsf(lx), oz = b->hz + PLAYER_R - fabsf(lz);
		if (ox > 0.0f && oz > 0.0f) {
			if (ox < oz) lx += signf(lx) * ox;
			else lz += signf(lz) * oz;
			p->x = b->cx + lx * b->c + lz * b->s;
			p->z = b->cz + lx * b->s - lz * b->c;
		}
	}
	/* Trees near the player. */
	int n;
	const tree_t *trees = world_trees(&n);
	for (int i = 0; i < n; i++) {
		const tree_t *t = &trees[i];
		if (fabsf(t->x - p->x) > 1.2f || fabsf(t->z - p->z) > 1.2f) continue;
		float r = (t->kind == TREE_ASPEN ? 0.14f : (t->kind == TREE_SNAG ? 0.2f : 0.3f)) + PLAYER_R;
		push_circle(p, t->x, t->z, r);
	}
	return edge;
}

v3 world_move(v3 from, v3 to, float feet_y, int *edge)
{
	v3 p = to;
	int why = blocked(from, p, feet_y);
	if (why) {
		/* Slide along whichever axis is still free. */
		v3 px = v3_make(to.x, to.y, from.z), pz = v3_make(from.x, to.y, to.z);
		if (!blocked(from, px, feet_y)) p = px;
		else if (!blocked(from, pz, feet_y)) p = pz;
		else p = from;
	}
	int wall_edge = push_out(&p, feet_y);
	/* A push must not shove us somewhere we couldn't have walked. */
	if (blocked(from, p, feet_y)) p = from;
	if (edge) *edge = why > 0 ? why : wall_edge;
	return p;
}
