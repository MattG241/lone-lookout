/*
 * paths.c -- trails and the creek as soft ribbons laid over the ground.
 *
 * The terrain's 4 m grid is far too coarse to paint a 1.8 m footpath, so each
 * trail is a strip draped on the exact terrain surface a few centimetres up:
 * opaque along its centre, feathering to nothing at its edges, lit by the
 * same light ramp as the ground. The creek is the same idea in water colours.
 */
#include <math.h>
#include "gfx/paths.h"
#include "gfx/gfx.h"
#include "gfx/mesh.h"
#include "world/world.h"
#include "palette_data.h"

#define STEP 1.25f
#define CHUNKS 8                      /* the terrain's chunks (terrain.c) */
#define CHUNK_SIZE (WORLD_SIZE / CHUNKS)
#define LOD_RANGE 96.0f               /* as terrain.c: half-resolution ground beyond this */
#define MAX_PIECES 320
#define MAX_SAMPLES 1024

/* A ribbon is cut where it crosses from one terrain chunk to the next, so
 * each piece is culled on its own and follows its chunk's level of detail:
 * near, a 1.25 m step on the exact surface; far, every other sample, lifted
 * clear of the coarser ground drawn there. */
typedef struct {
	dlist_t near, far;
	v3 centre;
	float radius;
	int ci, cj;                       /* its terrain chunk */
} piece_t;

static piece_t s_piece[MAX_PIECES];
static int s_n;

typedef struct {
	float half_w, lift;
	rgbf col;
	float alpha;
} ribbon_style;

static int resample(const float (*pts)[2], int n, float out[][2], int max_out)
{
	int k = 0;
	out[k][0] = pts[0][0];
	out[k][1] = pts[0][1];
	k++;
	for (int i = 0; i + 1 < n && k < max_out; i++) {
		float dx = pts[i + 1][0] - pts[i][0], dz = pts[i + 1][1] - pts[i][1];
		float len = sqrtf(dx * dx + dz * dz);
		int steps = (int)ceilf(len / STEP);
		for (int s = 1; s <= steps && k < max_out; s++, k++) {
			out[k][0] = pts[i][0] + dx * s / steps;
			out[k][1] = pts[i][1] + dz * s / steps;
		}
	}
	return k;
}

/* The half-resolution ground (8 m grid), bilinear: near enough to keep a far
 * ribbon above it. */
static float coarse_height(float x, float z)
{
	const float cell = WORLD_CELL * 2.0f;
	float fx = clampf(x / cell, 0.0f, WORLD_GRID / 2 - 0.001f), fz = clampf(z / cell, 0.0f, WORLD_GRID / 2 - 0.001f);
	int a = (int)fx, b = (int)fz;
	float tx = fx - a, tz = fz - b;
	float h00 = world_grid_height(2 * a, 2 * b), h10 = world_grid_height(2 * a + 2, 2 * b);
	float h01 = world_grid_height(2 * a, 2 * b + 2), h11 = world_grid_height(2 * a + 2, 2 * b + 2);
	return (h00 * (1 - tx) + h10 * tx) * (1 - tz) + (h01 * (1 - tx) + h11 * tx) * tz;
}

static int chunk_of(float v)
{
	int k = (int)(v / CHUNK_SIZE);
	return k < 0 ? 0 : (k >= CHUNKS ? CHUNKS - 1 : k);
}

/* The ribbon's cross-sections: left edge, centre, right edge, normal. */
typedef struct {
	v3 l, c, r, n;
} section_t;

static void record(dlist_t *dl, const section_t *sec, int i0, int i1, int step, u32 c_mid, u32 c_edge)
{
	int count = (i1 - i0 + step - 1) / step + 1;
	dl_begin(dl, count * (3 + 8 * 28) + 64, vdesc_pnc_flush);
	int prev = i0;
	for (int i = i0 + step;; i += step) {
		if (i > i1) i = i1;
		const section_t *a = &sec[prev], *b = &sec[i];
		GX_Begin(GX_QUADS, VF_3D, 8);
		emit_pnc(a->l, a->n, c_edge);
		emit_pnc(a->c, a->n, c_mid);
		emit_pnc(b->c, b->n, c_mid);
		emit_pnc(b->l, b->n, c_edge);
		emit_pnc(a->c, a->n, c_mid);
		emit_pnc(a->r, a->n, c_edge);
		emit_pnc(b->r, b->n, c_edge);
		emit_pnc(b->c, b->n, c_mid);
		GX_End();
		prev = i;
		if (i == i1) break;
	}
	dl_end(dl);
}

static void build_ribbon(const float (*pts)[2], int n, const ribbon_style *st)
{
	static float p[MAX_SAMPLES][2];
	static section_t near[MAX_SAMPLES], far[MAX_SAMPLES];
	int m = resample(pts, n, p, MAX_SAMPLES);
	if (m < 2) return;
	u32 c_mid = rgba_u32(st->col, st->alpha), c_edge = rgba_u32(st->col, 0.0f);
	for (int i = 0; i < m; i++) {
		int a = i > 0 ? i - 1 : i, b = i < m - 1 ? i + 1 : i;
		float tx = p[b][0] - p[a][0], tz = p[b][1] - p[a][1];
		float tl = sqrtf(tx * tx + tz * tz);
		tx /= tl;
		tz /= tl;
		float lx = tz, lz = -tx;                      /* left of the direction of travel */
		float x = p[i][0], z = p[i][1];
		v3 L = v3_make(x + lx * st->half_w, 0, z + lz * st->half_w);
		v3 R = v3_make(x - lx * st->half_w, 0, z - lz * st->half_w);
		near[i].n = far[i].n = world_normal(x, z);
		near[i].c = v3_make(x, world_height(x, z) + st->lift, z);
		near[i].l = v3_make(L.x, world_height(L.x, L.z) + st->lift * 0.7f, L.z);
		near[i].r = v3_make(R.x, world_height(R.x, R.z) + st->lift * 0.7f, R.z);
		far[i].c = v3_make(x, fmaxf(near[i].c.y, coarse_height(x, z) + 0.25f), z);
		far[i].l = v3_make(L.x, fmaxf(near[i].l.y, coarse_height(L.x, L.z) + 0.2f), L.z);
		far[i].r = v3_make(R.x, fmaxf(near[i].r.y, coarse_height(R.x, R.z) + 0.2f), R.z);
	}
	/* One piece per run of samples in the same chunk; each piece ends on the
	 * next one's first sample, so they join. */
	for (int i0 = 0; i0 < m - 1 && s_n < MAX_PIECES;) {
		int ci = chunk_of(p[i0][0]), cj = chunk_of(p[i0][1]);
		int i1 = i0 + 1;
		/* (at least 8 samples, so a trail along a chunk edge isn't cut to crumbs) */
		while (i1 < m - 1 && ((chunk_of(p[i1][0]) == ci && chunk_of(p[i1][1]) == cj) || i1 - i0 < 8)) i1++;
		piece_t *pc = &s_piece[s_n++];
		pc->ci = ci;
		pc->cj = cj;
		record(&pc->near, near, i0, i1, 1, c_mid, c_edge);
		record(&pc->far, far, i0, i1, 2, c_mid, c_edge);
		float minx = 1e9f, maxx = -1e9f, minz = 1e9f, maxz = -1e9f, miny = 1e9f, maxy = -1e9f;
		for (int i = i0; i <= i1; i++) {
			minx = fminf(minx, p[i][0]);
			maxx = fmaxf(maxx, p[i][0]);
			minz = fminf(minz, p[i][1]);
			maxz = fmaxf(maxz, p[i][1]);
			miny = fminf(miny, near[i].c.y);
			maxy = fmaxf(maxy, far[i].c.y);
		}
		pc->centre = v3_make((minx + maxx) * 0.5f, (miny + maxy) * 0.5f, (minz + maxz) * 0.5f);
		float ex = (maxx - minx) * 0.5f + 2.0f, ey = (maxy - miny) * 0.5f + 2.0f, ez = (maxz - minz) * 0.5f + 2.0f;
		pc->radius = sqrtf(ex * ex + ey * ey + ez * ez);
		i0 = i1;
	}
}

void paths_build(void)
{
	const ribbon_style trail = { 0.95f, 0.07f, rgb_scale(rgb_from(ALB_TRAIL), 1.06f), 0.88f };
	const ribbon_style creek = { 1.05f, 0.09f, rgb_lerp(rgb_from(ALB_WATER), rgb_make(0.30f, 0.38f, 0.42f), 0.35f), 0.92f };
	s_n = 0;
	for (int t = 0; t < world_trail_count(); t++) {
		int n;
		const float (*pts)[2] = world_trail(t, &n);
		build_ribbon(pts, n, &trail);
	}
	int n;
	const float (*cp)[2] = world_creek(&n);
	build_ribbon(cp, n, &creek);
}

/* Distance from the eye to a terrain chunk's footprint, as terrain.c measures it. */
static float chunk_dist(int ci, int cj)
{
	float x0 = ci * CHUNK_SIZE, z0 = cj * CHUNK_SIZE;
	float dx = fmaxf(fmaxf(x0 - G.eye.x, 0.0f), G.eye.x - (x0 + CHUNK_SIZE));
	float dz = fmaxf(fmaxf(z0 - G.eye.z, 0.0f), G.eye.z - (z0 + CHUNK_SIZE));
	return sqrtf(dx * dx + dz * dz);
}

void paths_draw(const daylight *d)
{
	gfx_model_identity();
	gfx_depth(true, false);
	gfx_blend_alpha();
	mat_lit_solid();
	GX_SetCullMode(GX_CULL_NONE);
	mat_ramp_light(d->key_dir);
	vdesc_pnc_flush();
	for (int i = 0; i < s_n; i++) {
		const piece_t *pc = &s_piece[i];
		if (gfx_sphere_visible(pc->centre, pc->radius))
			dl_draw(chunk_dist(pc->ci, pc->cj) < LOD_RANGE ? &pc->near : &pc->far);
	}
	gfx_blend_none();
	gfx_depth(true, true);
}
