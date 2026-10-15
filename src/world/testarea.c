/*
 * testarea.c -- the Phase 2 prototype map: a 200 m bowl around a blockout of
 * Kestrel Lookout. It exercises everything the real map will need --
 * terrain colour washes, trails, stairs and a deck to climb, trees to walk
 * between, a note to read and a log to sit on -- in a space small enough to
 * iterate on quickly.
 */
#include <math.h>
#include <string.h>
#include "world/world.h"
#include "core/noise.h"
#include "palette_data.h"

#define STEP_UP 0.6f
#define PLAYER_R 0.3f
#define MAX_TREES 160
#define MAX_TUFTS 900

static float s_pad_h;                         /* tower pad height */
static float s_bench_h;
static tree_t s_trees[MAX_TREES];
static int s_num_trees;
static tuft_t s_tufts[MAX_TUFTS];
static int s_num_tufts;
static interactable_t s_ia[2];

/* Trail centre lines (x, z pairs). */
static const float TRAIL_MAIN[][2] = { { 5, 34 }, { 4, 24 }, { 3, 15 }, { 1, 9 }, { 0, 5.5f } };
static const float TRAIL_BENCH[][2] = { { 2.5f, 12 }, { -4, 11 }, { -10, 9.5f }, { -13, 8.5f } };

static const float MEADOW_X = -24.0f, MEADOW_Z = 22.0f, MEADOW_R = 15.0f;
static const float BENCH_X = -14.5f, BENCH_Z = 8.0f;

static float dist2d(float ax, float az, float bx, float bz)
{
	float dx = ax - bx, dz = az - bz;
	return sqrtf(dx * dx + dz * dz);
}

static float seg_dist(float px, float pz, const float *a, const float *b)
{
	float vx = b[0] - a[0], vz = b[1] - a[1];
	float t = ((px - a[0]) * vx + (pz - a[1]) * vz) / (vx * vx + vz * vz);
	t = clampf(t, 0.0f, 1.0f);
	return dist2d(px, pz, a[0] + vx * t, a[1] + vz * t);
}

static float trail_dist(float x, float z)
{
	float d = 1e9f;
	for (unsigned i = 0; i + 1 < sizeof(TRAIL_MAIN) / sizeof(TRAIL_MAIN[0]); i++)
		d = fminf(d, seg_dist(x, z, TRAIL_MAIN[i], TRAIL_MAIN[i + 1]));
	for (unsigned i = 0; i + 1 < sizeof(TRAIL_BENCH) / sizeof(TRAIL_BENCH[0]); i++)
		d = fminf(d, seg_dist(x, z, TRAIL_BENCH[i], TRAIL_BENCH[i + 1]));
	return d;
}

static float raw_height(float x, float z)
{
	float r = sqrtf(x * x + z * z);
	float h = (fbm2(x * 0.018f + 11.0f, z * 0.018f, 4, 7) - 0.5f) * 7.0f;
	h += (fbm2(x * 0.09f, z * 0.09f, 2, 9) - 0.5f) * 0.7f;
	float rim = smoothstepf(40.0f, 98.0f, r);
	h += rim * rim * 16.0f * (0.6f + 0.8f * fbm2(x * 0.011f + 3.0f, z * 0.011f - 2.0f, 3, 5));
	/* A knoll to the north-east and a shallow swale through the meadow. */
	float kx = x - 40.0f, kz = z + 32.0f;
	h += 7.0f * expf(-(kx * kx + kz * kz) / (2.0f * 15.0f * 15.0f));
	float mx = x - MEADOW_X, mz = z - MEADOW_Z;
	h -= 1.2f * expf(-(mx * mx + mz * mz) / (2.0f * 12.0f * 12.0f));
	return h;
}

float world_height(float x, float z)
{
	float h = raw_height(x, z);
	float pad = 1.0f - smoothstepf(13.0f, 24.0f, dist2d(x, z, TOWER_X, TOWER_Z + 4.0f));
	h = lerpf(h, s_pad_h, pad);
	float bench = 1.0f - smoothstepf(2.0f, 5.0f, dist2d(x, z, BENCH_X, BENCH_Z));
	h = lerpf(h, s_bench_h, bench);
	h -= 0.12f * expf(-trail_dist(x, z) * 0.9f);
	return h;
}

v3 world_normal(float x, float z)
{
	const float e = 1.25f;
	float hx = world_height(x + e, z) - world_height(x - e, z);
	float hz = world_height(x, z + e) - world_height(x, z - e);
	return v3_norm(v3_make(-hx, 2.0f * e, -hz));
}

/* Large soft "washes" of colour, like watercolour laid over pencil. */
void world_vertex_color(float x, float z, float h, v3 n, rgbf *albedo, float *veg)
{
	rgbf summer = rgb_from(ALB_GRASS_SUMMER), autumn = rgb_from(ALB_GRASS_AUTUMN);
	float wash = smoothstepf(0.35f, 0.72f, fbm2(x * 0.035f + 4.0f, z * 0.035f, 3, 21));
	rgbf c = rgb_lerp(summer, autumn, 0.35f + 0.5f * wash);

	/* Needle-floor under forest stands: darker, olive-brown. */
	float stand = smoothstepf(0.45f, 0.62f, fbm2(x * 0.03f, z * 0.03f + 9.0f, 3, 33));
	stand *= smoothstepf(14.0f, 24.0f, dist2d(x, z, TOWER_X, TOWER_Z));
	rgbf floor = rgb_lerp(rgb_from(ALB_PINE_DARK), rgb_from(ALB_TRAIL), 0.45f);
	c = rgb_lerp(c, floor, stand * 0.75f);

	/* Meadow: a lighter, warmer floor. */
	float meadow = 1.0f - smoothstepf(MEADOW_R - 4.0f, MEADOW_R + 6.0f, dist2d(x, z, MEADOW_X, MEADOW_Z));
	c = rgb_lerp(c, rgb_lerp(autumn, rgb_make(0.86f, 0.80f, 0.58f), 0.3f), meadow * 0.6f);

	/* Rock on steep ground and up the bowl's rim. */
	float slope = sqrtf(n.x * n.x + n.z * n.z) / fmaxf(n.y, 0.05f);
	float rock = smoothstepf(0.45f, 0.8f, slope + (fbm2(x * 0.08f, z * 0.08f, 2, 41) - 0.5f) * 0.3f);
	c = rgb_lerp(c, rgb_from(ALB_ROCK), rock);

	/* Trail and the trodden tower pad. */
	float td = trail_dist(x, z) + (noise2(x * 0.6f, z * 0.6f, 51) - 0.5f) * 0.9f;
	float trail = 1.0f - smoothstepf(0.7f, 1.5f, td);
	float padd = dist2d(x, z, TOWER_X, TOWER_Z);
	trail = fmaxf(trail, (1.0f - smoothstepf(3.5f, 6.5f, padd)) * 0.7f);
	c = rgb_lerp(c, rgb_from(ALB_TRAIL), trail);

	/* Ambient occlusion from local concavity. */
	const float e = 3.0f;
	float lap = world_height(x + e, z) + world_height(x - e, z) + world_height(x, z + e) +
	            world_height(x, z - e) - 4.0f * h;
	float ao = clampf(1.0f - lap * 0.06f, 0.78f, 1.06f);
	c = rgb_scale(c, ao);

	*albedo = c;
	*veg = saturatef(1.0f - trail - rock);
}

/* ------------------------------------------------------------------------ */

static bool on_stairs_xz(float x, float z, float *stair_h)
{
	float zt = TOWER_Z + TOWER_DECK_HALF, zb = zt + STAIR_RUN;
	if (fabsf(x - STAIR_X) > STAIR_HALF_W || z < zt || z > zb) return false;
	*stair_h = s_pad_h + TOWER_DECK_Y * (zb - z) / STAIR_RUN;
	return true;
}

float world_ground(float x, float z, float y_ref, surface_t *surf)
{
	float best = world_height(x, z);
	surface_t s = trail_dist(x, z) < 1.2f || dist2d(x, z, TOWER_X, TOWER_Z) < 5.0f ? SURF_DIRT : SURF_GRASS;
	float hs;
	if (on_stairs_xz(x, z, &hs) && hs <= y_ref + STEP_UP && hs > best) {
		best = hs;
		s = SURF_STAIR;
	}
	if (fabsf(x - TOWER_X) <= TOWER_DECK_HALF && fabsf(z - TOWER_Z) <= TOWER_DECK_HALF) {
		float hd = s_pad_h + TOWER_DECK_Y;
		if (hd <= y_ref + STEP_UP && hd > best) {
			best = hd;
			s = SURF_WOOD;
		}
	}
	if (surf) *surf = s;
	return best;
}

static void push_out_circle(v3 *p, float cx, float cz, float r)
{
	float dx = p->x - cx, dz = p->z - cz;
	float d = sqrtf(dx * dx + dz * dz);
	if (d < r && d > 1e-4f) {
		p->x = cx + dx / d * r;
		p->z = cz + dz / d * r;
	}
}

static void push_out_box(v3 *p, float cx, float cz, float hx, float hz)
{
	float dx = p->x - cx, dz = p->z - cz;
	float ox = hx - fabsf(dx), oz = hz - fabsf(dz);
	if (ox > 0.0f && oz > 0.0f) {
		if (ox < oz) p->x += signf(dx) * ox;
		else p->z += signf(dz) * oz;
	}
}

static bool blocked(v3 from, v3 to, float feet_y)
{
	/* Too steep to walk up (about 38 degrees). */
	float h_to = world_height(to.x, to.z);
	if (feet_y < s_pad_h + 0.5f || h_to > feet_y + 0.05f) {
		v3 n = world_normal(to.x, to.z);
		float h_from = world_height(from.x, from.z);
		if (n.y < 0.79f && h_to > h_from) return true;
	}
	/* Walking into the side of the low end of the stair flight. */
	float hs;
	if (on_stairs_xz(to.x, to.z, &hs) && hs > feet_y + STEP_UP && hs - s_pad_h < 2.1f)
		return true;
	return false;
}

v3 world_move(v3 from, v3 to, float feet_y)
{
	v3 p = to;
	if (blocked(from, p, feet_y)) {
		/* Slide along whichever axis is still free. */
		v3 px = v3_make(to.x, to.y, from.z), pz = v3_make(from.x, to.y, to.z);
		if (!blocked(from, px, feet_y)) p = px;
		else if (!blocked(from, pz, feet_y)) p = pz;
		else p = from;
	}

	bool on_deck = feet_y > s_pad_h + TOWER_DECK_Y - 0.4f;
	float hs;
	bool on_flight = !on_deck && feet_y > s_pad_h + 0.35f && on_stairs_xz(from.x, from.z, &hs);

	if (on_deck) {
		/* Railing all round, except the stair head on the south side. */
		float lim = TOWER_DECK_HALF - PLAYER_R;
		bool at_stair_head = fabsf(p.x - STAIR_X) < STAIR_HALF_W - 0.2f && p.z > TOWER_Z;
		p.x = clampf(p.x, TOWER_X - lim, TOWER_X + lim);
		if (!at_stair_head) p.z = clampf(p.z, TOWER_Z - lim, TOWER_Z + lim);
		else p.z = fmaxf(p.z, TOWER_Z - lim);
		push_out_box(&p, TOWER_X, TOWER_Z, TOWER_CAB_HALF + PLAYER_R, TOWER_CAB_HALF + PLAYER_R);
	} else if (on_flight) {
		/* Handrails. */
		float lim = STAIR_HALF_W - PLAYER_R + 0.05f;
		p.x = clampf(p.x, STAIR_X - lim, STAIR_X + lim);
	} else {
		/* Tower legs. */
		for (int i = 0; i < 4; i++) {
			float sx = (i & 1) ? 1.0f : -1.0f, sz = (i & 2) ? 1.0f : -1.0f;
			push_out_circle(&p, TOWER_X + sx * TOWER_LEG_BASE, TOWER_Z + sz * TOWER_LEG_BASE, 0.22f + PLAYER_R);
		}
		/* Tree trunks. */
		for (int i = 0; i < s_num_trees; i++) {
			const tree_t *t = &s_trees[i];
			if (fabsf(t->x - p.x) < 2.0f && fabsf(t->z - p.z) < 2.0f)
				push_out_circle(&p, t->x, t->z, (t->kind == TREE_ASPEN ? 0.15f : 0.3f) + PLAYER_R);
		}
		/* The log bench. */
		push_out_box(&p, BENCH_X, BENCH_Z, 0.25f + PLAYER_R, 1.25f + PLAYER_R);
		/* Soft edge of the walkable bowl. */
		float r = sqrtf(p.x * p.x + p.z * p.z);
		if (r > WORLD_WALK_RADIUS) {
			p.x *= WORLD_WALK_RADIUS / r;
			p.z *= WORLD_WALK_RADIUS / r;
		}
	}
	return p;
}

/* ------------------------------------------------------------------------ */

static void place_vegetation(void)
{
	rng_t rng;
	rng_seed(&rng, 1998);
	s_num_trees = 0;
	for (int i = 0; i < 6000 && s_num_trees < MAX_TREES; i++) {
		float x = rng_range(&rng, -96.0f, 96.0f), z = rng_range(&rng, -96.0f, 96.0f);
		float r = sqrtf(x * x + z * z);
		if (r < 14.0f || r > 96.0f) continue;
		if (dist2d(x, z, TOWER_X, TOWER_Z) < 15.0f) continue;
		if (trail_dist(x, z) < 4.0f) continue;
		if (dist2d(x, z, BENCH_X, BENCH_Z) < 6.0f) continue;
		float dm = dist2d(x, z, MEADOW_X, MEADOW_Z);
		if (dm < MEADOW_R) continue;
		/* Keep the view from the start and from the bench open. */
		if (x > -6.0f && x < 14.0f && z > 6.0f && z < 40.0f) continue;
		if (x < -18.0f && x > -60.0f && z > -2.0f && z < 16.0f) continue;
		float stand = fbm2(x * 0.03f, z * 0.03f + 9.0f, 3, 33);
		float want = smoothstepf(0.42f, 0.6f, stand) * 0.85f + 0.06f;
		if (rng_float(&rng) > want) continue;
		v3 n = world_normal(x, z);
		if (n.y < 0.72f) continue;

		tree_t *t = &s_trees[s_num_trees++];
		t->x = x;
		t->z = z;
		t->y = world_height(x, z) - 0.15f;
		t->yaw = rng_range(&rng, 0.0f, LL_TAU);
		t->tint = rng_range(&rng, -1.0f, 1.0f);
		t->phase = rng_range(&rng, 0.0f, LL_TAU);
		float k = rng_float(&rng);
		if (dm < MEADOW_R + 14.0f && k < 0.6f) {
			t->kind = TREE_ASPEN;
			t->height = rng_range(&rng, 7.0f, 11.0f);
		} else if (k < 0.05f) {
			t->kind = TREE_SNAG;
			t->height = rng_range(&rng, 7.0f, 12.0f);
		} else {
			t->kind = TREE_CONIFER0 + (rng_next(&rng) & 3);
			t->height = rng_range(&rng, 8.0f, 17.0f);
		}
		t->width = t->height * 0.5f * rng_range(&rng, 0.85f, 1.1f);
	}

	s_num_tufts = 0;
	for (int i = 0; i < 9000 && s_num_tufts < MAX_TUFTS; i++) {
		float x = rng_range(&rng, -62.0f, 62.0f), z = rng_range(&rng, -62.0f, 62.0f);
		if (trail_dist(x, z) < 1.3f) continue;
		if (dist2d(x, z, TOWER_X, TOWER_Z) < 4.5f) continue;
		if (fabsf(x - STAIR_X) < 1.4f && z > TOWER_Z && z < TOWER_Z + 16.0f) continue;
		if (dist2d(x, z, BENCH_X, BENCH_Z) < 1.6f) continue;
		float dm = dist2d(x, z, MEADOW_X, MEADOW_Z);
		float dens = dm < MEADOW_R ? 0.95f : 0.35f + 0.3f * fbm2(x * 0.1f, z * 0.1f, 2, 61);
		if (rng_float(&rng) > dens) continue;
		tuft_t *t = &s_tufts[s_num_tufts++];
		t->x = x;
		t->z = z;
		t->y = world_height(x, z) - 0.04f;
		t->yaw = rng_range(&rng, 0.0f, LL_TAU);
		float k = rng_float(&rng);
		if (k < 0.04f) {
			t->kind = TUFT_ROCK;
			t->size = rng_range(&rng, 0.5f, 1.3f);
		} else if (k < (dm < MEADOW_R ? 0.45f : 0.12f)) {
			t->kind = TUFT_FLOWERS;
			t->size = rng_range(&rng, 0.5f, 0.85f);
		} else {
			t->kind = TUFT_GRASS;
			t->size = rng_range(&rng, 0.45f, 0.9f);
		}
	}
}

void world_init(void)
{
	s_pad_h = raw_height(TOWER_X, TOWER_Z + 4.0f);
	s_bench_h = raw_height(BENCH_X, BENCH_Z);
	place_vegetation();

	/* Augie's welcome note, pinned to the bottom stair post. */
	float zb = TOWER_Z + TOWER_DECK_HALF + STAIR_RUN;
	memset(s_ia, 0, sizeof(s_ia));
	s_ia[0].pos = v3_make(STAIR_X + STAIR_HALF_W + 0.08f, s_pad_h + 1.2f, zb + 0.055f);
	s_ia[0].radius = 0.35f;
	s_ia[0].kind = IA_NOTE;
	s_ia[0].verb = "Read";
	s_ia[0].id = 0;

	/* A fallen log by the trail, facing the sunset. */
	float bh = world_height(BENCH_X, BENCH_Z);
	s_ia[1].pos = v3_make(BENCH_X, bh + 0.45f, BENCH_Z);
	s_ia[1].radius = 1.0f;
	s_ia[1].kind = IA_SIT;
	s_ia[1].verb = "Sit";
	s_ia[1].id = 1;
	s_ia[1].sit_eye = v3_make(BENCH_X, bh + 0.45f + 0.78f, BENCH_Z);
	s_ia[1].sit_yaw = -LL_PI * 0.5f;     /* west */
}

const tree_t *world_trees(int *count) { *count = s_num_trees; return s_trees; }
const tuft_t *world_tufts(int *count) { *count = s_num_tufts; return s_tufts; }
const interactable_t *world_interactables(int *count) { *count = 2; return s_ia; }

float world_pad_height(void) { return s_pad_h; }
v3 world_bench_pos(void) { return v3_make(BENCH_X, world_height(BENCH_X, BENCH_Z), BENCH_Z); }
