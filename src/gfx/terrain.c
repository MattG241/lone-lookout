/*
 * terrain.c -- the Ashen Peaks ground: 8 x 8 chunks of 16 x 16 cells, each a
 * display list of triangle strips, plus per-chunk lists of cut-paper tufts.
 *
 * Vertex colour = baked albedo (late summer and early autumn, blended by the
 * season) with cavity AO; light comes from the ramp in mat_lit_solid().
 * Chunks on the map's edge get a skirt hanging down, so a glimpse past the
 * rim never shows the underside of the world.
 *
 * The season changes once a day (behind the day card), so instead of a
 * per-vertex blend in the TEV, the lists are simply re-recorded.
 */
#include <malloc.h>
#include <math.h>
#include "gfx/terrain.h"
#include "gfx/gfx.h"
#include "gfx/mesh.h"
#include "world/world.h"
#include "assets.h"
#include "palette_data.h"

#define CHUNKS 8
#define CHUNK_CELLS (WORLD_GRID / CHUNKS)       /* 16 */
#define SKIRT 40.0f
#define TUFT_RANGE 62.0f

typedef struct {
	dlist_t ground;
	dlist_t tufts;
	v3 centre;
	float radius;
	float x0, z0;               /* NW corner */
	int tuft_first, tuft_count; /* into s_tuft_order */
} chunk_t;

static chunk_t s_chunks[CHUNKS][CHUNKS];
static int *s_tuft_order;       /* tuft indices sorted by chunk */
static float s_season;

static float gx(int i) { return i * WORLD_CELL; }

static void emit_vertex(int i, int j)
{
	v3 p = v3_make(gx(i), world_grid_height(i, j), gx(j));
	emit_pnc(p, world_grid_normal(i, j), world_grid_color(i, j, s_season));
}

/* One side of the map's edge: a vertical curtain under the edge vertices. */
static void emit_skirt(int ci, int cj)
{
	int i0 = ci * CHUNK_CELLS, j0 = cj * CHUNK_CELLS;
	for (int side = 0; side < 4; side++) {
		bool on = (side == 0 && cj == 0) || (side == 1 && cj == CHUNKS - 1) ||
		          (side == 2 && ci == 0) || (side == 3 && ci == CHUNKS - 1);
		if (!on) continue;
		v3 n = side == 0 ? v3_make(0, 0, -1) : side == 1 ? v3_make(0, 0, 1) : side == 2 ? v3_make(-1, 0, 0) : v3_make(1, 0, 0);
		GX_Begin(GX_TRIANGLESTRIP, VF_3D, (CHUNK_CELLS + 1) * 2);
		for (int k = 0; k <= CHUNK_CELLS; k++) {
			/* Order the strip so the curtain faces outward. */
			int kk = (side == 1 || side == 2) ? k : CHUNK_CELLS - k;
			int i = side <= 1 ? i0 + kk : (side == 2 ? 0 : WORLD_GRID);
			int j = side <= 1 ? (side == 0 ? 0 : WORLD_GRID) : j0 + kk;
			float h = world_grid_height(i, j);
			u32 c = world_grid_color(i, j, s_season);
			u32 dark = (((c >> 24) & 0xFF) * 3 / 4) << 24 | (((c >> 16) & 0xFF) * 3 / 4) << 16 |
			           (((c >> 8) & 0xFF) * 3 / 4) << 8 | 0xFF;
			emit_pnc(v3_make(gx(i), h, gx(j)), n, c);
			emit_pnc(v3_make(gx(i), h - SKIRT, gx(j)), n, dark);
		}
		GX_End();
	}
}

static u32 ground_bytes(void)
{
	int strip_verts = (CHUNK_CELLS + 1) * 2;
	return CHUNK_CELLS * (3 + strip_verts * 28) + 4 * (3 + strip_verts * 28) + 96;
}

static void record_ground(chunk_t *c, int ci, int cj, bool again)
{
	if (again) dl_rebegin(&c->ground, vdesc_pnc_flush);
	else dl_begin(&c->ground, ground_bytes(), vdesc_pnc_flush);
	for (int r = 0; r < CHUNK_CELLS; r++) {
		int j = cj * CHUNK_CELLS + r;
		GX_Begin(GX_TRIANGLESTRIP, VF_3D, (CHUNK_CELLS + 1) * 2);
		for (int k = 0; k <= CHUNK_CELLS; k++) {
			int i = ci * CHUNK_CELLS + k;
			emit_vertex(i, j);
			emit_vertex(i, j + 1);
		}
		GX_End();
	}
	emit_skirt(ci, cj);
	dl_end(&c->ground);
}

static void chunk_bounds(chunk_t *c, int ci, int cj)
{
	float hmin = 1e9f, hmax = -1e9f;
	for (int r = 0; r <= CHUNK_CELLS; r++)
		for (int k = 0; k <= CHUNK_CELLS; k++) {
			float h = world_grid_height(ci * CHUNK_CELLS + k, cj * CHUNK_CELLS + r);
			hmin = fminf(hmin, h);
			hmax = fmaxf(hmax, h);
		}
	bool edge = ci == 0 || cj == 0 || ci == CHUNKS - 1 || cj == CHUNKS - 1;
	if (edge) hmin -= SKIRT;
	float size = CHUNK_CELLS * WORLD_CELL;
	c->x0 = gx(ci * CHUNK_CELLS);
	c->z0 = gx(cj * CHUNK_CELLS);
	c->centre = v3_make(c->x0 + size * 0.5f, (hmin + hmax) * 0.5f, c->z0 + size * 0.5f);
	float hh = (hmax - hmin) * 0.5f;
	c->radius = sqrtf(size * size * 0.5f + hh * hh);
}

/* ------------------------------------------------------------------------ */
/* Tufts                                                                    */
/* ------------------------------------------------------------------------ */

static void atlas_uv(int x, int y, int w, int h, float *u0, float *v0, float *u1, float *v1)
{
	*u0 = (x + 0.5f) / TEX_FOLIAGE_W;
	*v0 = (y + 0.5f) / TEX_FOLIAGE_H;
	*u1 = (x + w - 0.5f) / TEX_FOLIAGE_W;
	*v1 = (y + h - 0.5f) / TEX_FOLIAGE_H;
}

typedef struct {
	int cell[4];
	float w, h;                 /* width and height per unit size */
} tuft_style;

static const tuft_style STYLES[TUFT_KINDS] = {
	[TUFT_GRASS] = { { ATLAS_GRASS }, 1.1f, 0.95f },
	[TUFT_FLOWERS] = { { ATLAS_FLOWERS }, 1.1f, 1.0f },
	[TUFT_ROCK] = { { ATLAS_ROCK }, 1.0f, 0.8f },
	[TUFT_FERN] = { { ATLAS_FERN }, 1.2f, 0.9f },
	[TUFT_SAPLING] = { { ATLAS_CONIFER3 }, 0.55f, 1.1f },
	[TUFT_FIREWEED] = { { ATLAS_FLOWERS }, 0.8f, 1.35f },
	[TUFT_SEDGE] = { { ATLAS_GRASS }, 0.9f, 1.05f },
};

/* Bottom and top colours of a tuft (the atlas gives the shape and detail). */
static void tuft_colours(const tuft_t *t, float season, rgbf *bot, rgbf *top)
{
	rgbf grass = rgb_lerp(rgb_from(ALB_GRASS_SUMMER), rgb_from(ALB_GRASS_AUTUMN),
	                      saturatef(0.15f + season * 0.75f + (t->shade - 0.5f) * 0.3f));
	rgbf b = grass, tp = grass;
	switch (t->kind) {
	case TUFT_FLOWERS:
		tp = rgb_lerp(rgb_from(ALB_LARKSPUR), rgb_make(0.55f, 0.47f, 0.50f), season);
		if (t->shade > 0.7f) tp = rgb_from(ALB_YARROW);
		break;
	case TUFT_ROCK:
		b = tp = rgb_scale(rgb_from(ALB_ROCK), 0.9f + 0.2f * t->shade);
		break;
	case TUFT_FERN:
		b = tp = rgb_lerp(rgb_make(0.42f, 0.53f, 0.36f), rgb_make(0.66f, 0.47f, 0.30f), season);
		break;
	case TUFT_SAPLING:
		b = tp = rgb_scale(rgb_lerp(rgb_from(ALB_PINE), rgb_make(0.45f, 0.60f, 0.42f), 0.5f), 0.95f + 0.1f * t->shade);
		break;
	case TUFT_FIREWEED:
		b = rgb_lerp(rgb_make(0.45f, 0.52f, 0.36f), rgb_make(0.55f, 0.36f, 0.28f), season);
		tp = rgb_lerp(rgb_make(0.84f, 0.46f, 0.64f), rgb_make(0.74f, 0.33f, 0.27f), season);
		break;
	case TUFT_SEDGE:
		b = tp = rgb_lerp(rgb_make(0.47f, 0.54f, 0.36f), rgb_make(0.62f, 0.55f, 0.38f), season);
		break;
	default:
		break;
	}
	*bot = rgb_scale(b, 0.62f);
	*top = rgb_scale(tp, 1.08f);
}

static void record_tufts(chunk_t *c, bool again)
{
	int count;
	const tuft_t *tufts = world_tufts(&count);
	if (!c->tuft_count) {
		c->tufts.len = 0;
		return;
	}
	if (again) dl_rebegin(&c->tufts, vdesc_pnct_flush);
	else dl_begin(&c->tufts, c->tuft_count * (3 + 8 * 36) + 64, vdesc_pnct_flush);
	for (int n = 0; n < c->tuft_count; n++) {
		const tuft_t *t = &tufts[s_tuft_order[c->tuft_first + n]];
		const tuft_style *st = &STYLES[t->kind];
		float u0, v0, u1, v1;
		atlas_uv(st->cell[0], st->cell[1], st->cell[2], st->cell[3], &u0, &v0, &u1, &v1);
		float w = t->size * st->w, h = t->size * st->h;
		v3 nrm = world_normal(t->x, t->z);
		rgbf bot, top;
		tuft_colours(t, s_season, &bot, &top);
		u32 cb = rgba_u32(bot, 1.0f), ct = rgba_u32(top, 1.0f);
		GX_Begin(GX_QUADS, VF_3D, 8);
		for (int q = 0; q < 2; q++) {
			float a = t->yaw + q * (LL_PI * 0.5f);
			float dx = cosf(a) * w * 0.5f, dz = sinf(a) * w * 0.5f;
			v3 bl = v3_make(t->x - dx, world_height(t->x - dx, t->z - dz) - 0.04f, t->z - dz);
			v3 br = v3_make(t->x + dx, world_height(t->x + dx, t->z + dz) - 0.04f, t->z + dz);
			v3 tl = v3_make(bl.x, t->y + h, bl.z), tr = v3_make(br.x, t->y + h, br.z);
			emit_pnct(bl, nrm, cb, u0, v1);
			emit_pnct(br, nrm, cb, u1, v1);
			emit_pnct(tr, nrm, ct, u1, v0);
			emit_pnct(tl, nrm, ct, u0, v0);
		}
		GX_End();
	}
	dl_end(&c->tufts);
}

static void sort_tufts(void)
{
	int count;
	const tuft_t *tufts = world_tufts(&count);
	s_tuft_order = malloc(sizeof(int) * (count ? count : 1));
	int fill = 0;
	for (int cj = 0; cj < CHUNKS; cj++)
		for (int ci = 0; ci < CHUNKS; ci++) {
			chunk_t *c = &s_chunks[cj][ci];
			float size = CHUNK_CELLS * WORLD_CELL;
			c->tuft_first = fill;
			for (int i = 0; i < count; i++) {
				int ti = (int)(tufts[i].x / size), tj = (int)(tufts[i].z / size);
				ti = ti < 0 ? 0 : (ti >= CHUNKS ? CHUNKS - 1 : ti);
				tj = tj < 0 ? 0 : (tj >= CHUNKS ? CHUNKS - 1 : tj);
				if (ti == ci && tj == cj) s_tuft_order[fill++] = i;
			}
			c->tuft_count = fill - c->tuft_first;
		}
}

/* ------------------------------------------------------------------------ */

void terrain_build(float season)
{
	s_season = season;
	sort_tufts();
	for (int cj = 0; cj < CHUNKS; cj++)
		for (int ci = 0; ci < CHUNKS; ci++) {
			chunk_t *c = &s_chunks[cj][ci];
			chunk_bounds(c, ci, cj);
			record_ground(c, ci, cj, false);
			record_tufts(c, false);
		}
}

void terrain_set_season(float season)
{
	s_season = season;
	for (int cj = 0; cj < CHUNKS; cj++)
		for (int ci = 0; ci < CHUNKS; ci++) {
			record_ground(&s_chunks[cj][ci], ci, cj, true);
			record_tufts(&s_chunks[cj][ci], true);
		}
}

/* Distance from the eye to a chunk's footprint (0 when standing on it). */
static float chunk_dist(const chunk_t *c)
{
	float size = CHUNK_CELLS * WORLD_CELL;
	float dx = fmaxf(fmaxf(c->x0 - G.eye.x, 0.0f), G.eye.x - (c->x0 + size));
	float dz = fmaxf(fmaxf(c->z0 - G.eye.z, 0.0f), G.eye.z - (c->z0 + size));
	return sqrtf(dx * dx + dz * dz);
}

int terrain_draw(const daylight *d)
{
	gfx_model_identity();
	gfx_depth(true, true);
	gfx_blend_none();
	mat_lit_solid();
	mat_ramp_light(d->key_dir);
	vdesc_pnc_flush();
	int drawn = 0;
	for (int cj = 0; cj < CHUNKS; cj++)
		for (int ci = 0; ci < CHUNKS; ci++)
			if (gfx_sphere_visible(s_chunks[cj][ci].centre, s_chunks[cj][ci].radius)) {
				dl_draw(&s_chunks[cj][ci].ground);
				drawn++;
			}
	return drawn;
}

void terrain_draw_tufts(const daylight *d)
{
	gfx_model_identity();
	gfx_depth(true, true);
	gfx_blend_none();
	mat_lit_cutout();
	mat_ramp_light(d->key_dir);
	GX_SetTevKColor(GX_KCOLOR0, (GXColor){ 255, 255, 255, 255 });
	vdesc_pnct_flush();
	for (int cj = 0; cj < CHUNKS; cj++)
		for (int ci = 0; ci < CHUNKS; ci++) {
			chunk_t *c = &s_chunks[cj][ci];
			if (c->tufts.len && chunk_dist(c) < TUFT_RANGE && gfx_sphere_visible(c->centre, c->radius))
				dl_draw(&c->tufts);
		}
}

bool terrain_chunk_near(float x, float z, float range)
{
	float size = CHUNK_CELLS * WORLD_CELL;
	int ci = (int)(x / size), cj = (int)(z / size);
	ci = ci < 0 ? 0 : (ci >= CHUNKS ? CHUNKS - 1 : ci);
	cj = cj < 0 ? 0 : (cj >= CHUNKS ? CHUNKS - 1 : cj);
	return chunk_dist(&s_chunks[cj][ci]) < range;
}
