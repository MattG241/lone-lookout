/*
 * ashen.c -- the Ashen Peaks: baked terrain, ground types, trails, places,
 * and the tree and tuft placements.
 *
 * Heights are interpolated across the same triangles the renderer draws
 * (each 4 m cell split along its p01-p10 diagonal), so feet, props and
 * foliage sit exactly on the visible ground.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "world/world.h"
#include "world/world_int.h"
#include "world/blobs.h"

#define NV WORLD_VERTS
#define IDX(i, j) ((j) * NV + (i))

static float s_h[NV * NV];
static const u8 *s_col;                  /* 2 x NV x NV RGBA8: summer, autumn */
static const u8 *s_surf;                 /* WORLD_GRID^2 */
static tree_t *s_trees;
static int s_num_trees;
static tuft_t *s_tufts;
static int s_num_tufts;

static const world_place AREAS[AREA_COUNT] = WORLD_AREAS_INIT;
static const world_place LANDMARKS[] = WORLD_LANDMARKS_INIT;
static const float TRAIL_PTS[WORLD_TRAIL_POINTS][2] = WORLD_TRAIL_PTS_INIT;
static const struct { const char *name; int first, count; } TRAILS[WORLD_NUM_TRAILS] = WORLD_TRAILS_INIT;
static const float CREEK[WORLD_CREEK_POINTS][2] = WORLD_CREEK_INIT;
#if WORLD_NUM_BRIDGES > 0
static const float BRIDGES[WORLD_NUM_BRIDGES][3] = WORLD_BRIDGES_INIT;
#endif

/* ------------------------------------------------------------------------ */
/* Terrain                                                                  */
/* ------------------------------------------------------------------------ */

static void cell_of(float x, float z, int *i, int *j, float *fx, float *fz)
{
	float gx = clampf(x / WORLD_CELL, 0.0f, (float)WORLD_GRID - 1e-3f);
	float gz = clampf(z / WORLD_CELL, 0.0f, (float)WORLD_GRID - 1e-3f);
	*i = (int)gx;
	*j = (int)gz;
	*fx = gx - (float)*i;
	*fz = gz - (float)*j;
}

float world_height(float x, float z)
{
	int i, j;
	float fx, fz;
	cell_of(x, z, &i, &j, &fx, &fz);
	float h00 = s_h[IDX(i, j)], h10 = s_h[IDX(i + 1, j)];
	float h01 = s_h[IDX(i, j + 1)], h11 = s_h[IDX(i + 1, j + 1)];
	if (fx + fz <= 1.0f)
		return h00 + (h10 - h00) * fx + (h01 - h00) * fz;
	return h11 + (h01 - h11) * (1.0f - fx) + (h10 - h11) * (1.0f - fz);
}

v3 world_normal(float x, float z)
{
	int i, j;
	float fx, fz;
	cell_of(x, z, &i, &j, &fx, &fz);
	float h00 = s_h[IDX(i, j)], h10 = s_h[IDX(i + 1, j)];
	float h01 = s_h[IDX(i, j + 1)], h11 = s_h[IDX(i + 1, j + 1)];
	float dx, dz;
	if (fx + fz <= 1.0f) {
		dx = h10 - h00;
		dz = h01 - h00;
	} else {
		dx = h11 - h01;
		dz = h11 - h10;
	}
	return v3_norm(v3_make(-dx, WORLD_CELL, -dz));
}

float world_grid_height(int i, int j)
{
	i = i < 0 ? 0 : (i >= NV ? NV - 1 : i);
	j = j < 0 ? 0 : (j >= NV ? NV - 1 : j);
	return s_h[IDX(i, j)];
}

v3 world_grid_normal(int i, int j)
{
	/* Smooth vertex normal from central differences (for lighting only). */
	float dx = world_grid_height(i + 1, j) - world_grid_height(i - 1, j);
	float dz = world_grid_height(i, j + 1) - world_grid_height(i, j - 1);
	return v3_norm(v3_make(-dx, 2.0f * WORLD_CELL, -dz));
}

u32 world_grid_color(int i, int j, float season)
{
	i = i < 0 ? 0 : (i >= NV ? NV - 1 : i);
	j = j < 0 ? 0 : (j >= NV ? NV - 1 : j);
	const u8 *a = s_col + IDX(i, j) * 4, *b = a + NV * NV * 4;
	int t = (int)(saturatef(season) * 256.0f);
	u32 out = 0;
	for (int k = 0; k < 4; k++)
		out = (out << 8) | (u32)((a[k] * (256 - t) + b[k] * t) >> 8);
	return out;
}

static float seg_dist(float px, float pz, const float *a, const float *b)
{
	float vx = b[0] - a[0], vz = b[1] - a[1];
	float l2 = vx * vx + vz * vz;
	float t = l2 > 0.0f ? ((px - a[0]) * vx + (pz - a[1]) * vz) / l2 : 0.0f;
	t = clampf(t, 0.0f, 1.0f);
	float dx = px - (a[0] + vx * t), dz = pz - (a[1] + vz * t);
	return sqrtf(dx * dx + dz * dz);
}

float world_trail_dist(float x, float z)
{
	float d = 1e9f;
	for (int t = 0; t < WORLD_NUM_TRAILS; t++) {
		const float (*p)[2] = &TRAIL_PTS[TRAILS[t].first];
		for (int k = 0; k + 1 < TRAILS[t].count; k++) {
			/* Cheap reject on the segment's bounding box. */
			float x0 = fminf(p[k][0], p[k + 1][0]) - d, x1 = fmaxf(p[k][0], p[k + 1][0]) + d;
			float z0 = fminf(p[k][1], p[k + 1][1]) - d, z1 = fmaxf(p[k][1], p[k + 1][1]) + d;
			if (x < x0 || x > x1 || z < z0 || z > z1) continue;
			d = fminf(d, seg_dist(x, z, p[k], p[k + 1]));
		}
	}
	return d;
}

surface_t world_surface(float x, float z)
{
	if (world_trail_dist(x, z) < 1.3f) return SURF_DIRT;
	int i = (int)clampf(x / WORLD_CELL, 0.0f, WORLD_GRID - 1);
	int j = (int)clampf(z / WORLD_CELL, 0.0f, WORLD_GRID - 1);
	return (surface_t)s_surf[j * WORLD_GRID + i];
}

int world_trail_count(void) { return WORLD_NUM_TRAILS; }

const float (*world_trail(int t, int *count))[2]
{
	*count = TRAILS[t].count;
	return &TRAIL_PTS[TRAILS[t].first];
}

const float (*world_creek(int *count))[2]
{
	*count = WORLD_CREEK_POINTS;
	return CREEK;
}

int world_bridges(const float (**out)[3])
{
#if WORLD_NUM_BRIDGES > 0
	*out = BRIDGES;
	return WORLD_NUM_BRIDGES;
#else
	*out = 0;
	return 0;
#endif
}

/* ------------------------------------------------------------------------ */
/* Places                                                                   */
/* ------------------------------------------------------------------------ */

const world_place *world_areas(void) { return AREAS; }

const world_place *world_landmarks(int *count)
{
	*count = (int)(sizeof(LANDMARKS) / sizeof(LANDMARKS[0]));
	return LANDMARKS;
}

int world_area_at(float x, float z)
{
	int best = -1;
	float best_f = 1.0f;
	for (int a = 0; a < AREA_COUNT; a++) {
		float dx = x - AREAS[a].x, dz = z - AREAS[a].z;
		float f = sqrtf(dx * dx + dz * dz) / AREAS[a].radius;
		if (f < best_f) {
			best_f = f;
			best = a;
		}
	}
	return best;
}

/* ------------------------------------------------------------------------ */
/* Foliage                                                                  */
/* ------------------------------------------------------------------------ */

static float unit_hash(u32 n)
{
	n = (n ^ 61u) ^ (n >> 16);
	n *= 9u;
	n ^= n >> 4;
	n *= 0x27d4eb2du;
	n ^= n >> 15;
	return (n & 0xFFFFFFu) / 16777216.0f;
}

static void load_foliage(void)
{
	blob_t b = world_blob(BLOB_TREES);
	s_num_trees = (int)(b.size / 8);
	s_trees = malloc(sizeof(tree_t) * (s_num_trees ? s_num_trees : 1));
	for (int i = 0; i < s_num_trees; i++) {
		const u8 *r = b.data + i * 8;
		tree_t *t = &s_trees[i];
		t->x = be_u16(r) / 64.0f;
		t->z = be_u16(r + 2) / 64.0f;
		t->kind = r[4] < TREE_KINDS ? r[4] : TREE_CONIFER0;
		t->height = r[5] / 10.0f;
		t->yaw = r[6] * (LL_TAU / 256.0f);
		t->tint = r[7] / 127.5f - 1.0f;
		t->phase = unit_hash((u32)i * 7919u + 17u) * LL_TAU;
		float aspect = t->kind == TREE_SNAG ? 0.42f : (t->kind == TREE_ASPEN ? 0.48f : 0.5f);
		t->width = t->height * aspect * (0.88f + 0.22f * unit_hash((u32)i * 104729u + 3u));
		/* Sink the base a little so slopes never show daylight under a trunk. */
		t->y = world_height(t->x, t->z) - 0.25f;
	}

	b = world_blob(BLOB_TUFTS);
	s_num_tufts = (int)(b.size / 8);
	s_tufts = malloc(sizeof(tuft_t) * (s_num_tufts ? s_num_tufts : 1));
	for (int i = 0; i < s_num_tufts; i++) {
		const u8 *r = b.data + i * 8;
		tuft_t *t = &s_tufts[i];
		t->x = be_u16(r) / 64.0f;
		t->z = be_u16(r + 2) / 64.0f;
		t->kind = r[4] < TUFT_KINDS ? r[4] : TUFT_GRASS;
		t->size = r[5] / 100.0f;
		t->yaw = r[6] * (LL_TAU / 256.0f);
		t->shade = r[7] / 255.0f;
		t->y = world_height(t->x, t->z) - 0.05f;
	}
}

const tree_t *world_trees(int *count) { *count = s_num_trees; return s_trees; }
const tuft_t *world_tufts(int *count) { *count = s_num_tufts; return s_tufts; }

/* ------------------------------------------------------------------------ */

void world_init(void)
{
	blob_t b = world_blob(BLOB_HEIGHT);
	for (int k = 0; k < NV * NV && (u32)(k * 4 + 3) < b.size; k++)
		s_h[k] = be_f32(b.data + k * 4);
	s_col = world_blob(BLOB_COLOR).data;
	s_surf = world_blob(BLOB_SURFACE).data;
	load_foliage();
	walk_init();
	places_init();
}
