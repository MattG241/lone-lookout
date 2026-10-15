/*
 * terrain.c -- the ground as 4 x 4 chunks of triangle strips, each recorded
 * into a display list, plus per-chunk lists of cut-paper grass tufts.
 *
 * Vertex colour = painted albedo x baked AO (from world_vertex_color);
 * alpha = vegetation weight (reserved for the Phase 3 season tint).
 * Lighting comes entirely from the light ramp in mat_lit_solid().
 */
#include <malloc.h>
#include <math.h>
#include "gfx/terrain.h"
#include "gfx/gfx.h"
#include "gfx/mesh.h"
#include "world/world.h"
#include "assets.h"
#include "palette_data.h"

#define CHUNKS 4
#define CHUNK_CELLS ((WORLD_VERTS - 1) / CHUNKS)    /* 20 */

typedef struct {
	dlist_t ground;
	dlist_t tufts;
	v3 centre;
	float radius;
} chunk_t;

static chunk_t s_chunks[CHUNKS][CHUNKS];
static float *s_h;          /* WORLD_VERTS^2 heights */
static v3 *s_n;
static u32 *s_c;

#define IDX(i, j) ((j) * WORLD_VERTS + (i))

static float grid_x(int i) { return -WORLD_HALF + i * WORLD_CELL; }

static void build_grid(void)
{
	int n = WORLD_VERTS * WORLD_VERTS;
	s_h = malloc(sizeof(float) * n);
	s_n = malloc(sizeof(v3) * n);
	s_c = malloc(sizeof(u32) * n);
	for (int j = 0; j < WORLD_VERTS; j++)
		for (int i = 0; i < WORLD_VERTS; i++)
			s_h[IDX(i, j)] = world_height(grid_x(i), grid_x(j));
	for (int j = 0; j < WORLD_VERTS; j++) {
		for (int i = 0; i < WORLD_VERTS; i++) {
			int i0 = i > 0 ? i - 1 : i, i1 = i < WORLD_VERTS - 1 ? i + 1 : i;
			int j0 = j > 0 ? j - 1 : j, j1 = j < WORLD_VERTS - 1 ? j + 1 : j;
			float dx = (s_h[IDX(i1, j)] - s_h[IDX(i0, j)]) / ((i1 - i0) * WORLD_CELL);
			float dz = (s_h[IDX(i, j1)] - s_h[IDX(i, j0)]) / ((j1 - j0) * WORLD_CELL);
			v3 nn = v3_norm(v3_make(-dx, 1.0f, -dz));
			s_n[IDX(i, j)] = nn;
			rgbf alb;
			float veg;
			world_vertex_color(grid_x(i), grid_x(j), s_h[IDX(i, j)], nn, &alb, &veg);
			s_c[IDX(i, j)] = rgba_u32(alb, veg);
		}
	}
}

static void record_ground(chunk_t *c, int ci, int cj)
{
	int strip_verts = (CHUNK_CELLS + 1) * 2;
	u32 cap = CHUNK_CELLS * (3 + strip_verts * 28) + 64;
	dl_begin(&c->ground, cap, vdesc_pnc_flush);
	for (int r = 0; r < CHUNK_CELLS; r++) {
		int j = cj * CHUNK_CELLS + r;
		GX_Begin(GX_TRIANGLESTRIP, VF_3D, strip_verts);
		for (int k = 0; k <= CHUNK_CELLS; k++) {
			int i = ci * CHUNK_CELLS + k;
			for (int t = 0; t < 2; t++) {
				int jj = j + t;
				v3 p = v3_make(grid_x(i), s_h[IDX(i, jj)], grid_x(jj));
				emit_pnc(p, s_n[IDX(i, jj)], s_c[IDX(i, jj)]);
			}
		}
		GX_End();
	}
	dl_end(&c->ground);

	float x0 = grid_x(ci * CHUNK_CELLS), z0 = grid_x(cj * CHUNK_CELLS);
	float size = CHUNK_CELLS * WORLD_CELL;
	float hmin = 1e9f, hmax = -1e9f;
	for (int r = 0; r <= CHUNK_CELLS; r++)
		for (int k = 0; k <= CHUNK_CELLS; k++) {
			float h = s_h[IDX(ci * CHUNK_CELLS + k, cj * CHUNK_CELLS + r)];
			hmin = fminf(hmin, h);
			hmax = fmaxf(hmax, h);
		}
	c->centre = v3_make(x0 + size * 0.5f, (hmin + hmax) * 0.5f, z0 + size * 0.5f);
	float hh = (hmax - hmin) * 0.5f;
	c->radius = sqrtf(size * size * 0.5f + hh * hh);
}

/* Atlas cell -> UV rectangle */
static void atlas_uv(int x, int y, int w, int h, float *u0, float *v0, float *u1, float *v1)
{
	*u0 = (x + 0.5f) / TEX_FOLIAGE_W;
	*v0 = (y + 0.5f) / TEX_FOLIAGE_H;
	*u1 = (x + w - 0.5f) / TEX_FOLIAGE_W;
	*v1 = (y + h - 0.5f) / TEX_FOLIAGE_H;
}

static void tuft_cell(int kind, float *u0, float *v0, float *u1, float *v1)
{
	static const int cells[TUFT_KINDS][4] = { { ATLAS_GRASS }, { ATLAS_FLOWERS }, { ATLAS_ROCK } };
	atlas_uv(cells[kind][0], cells[kind][1], cells[kind][2], cells[kind][3], u0, v0, u1, v1);
}

static void record_tufts(chunk_t *c, int ci, int cj, const tuft_t *tufts, int count)
{
	float x0 = grid_x(ci * CHUNK_CELLS), z0 = grid_x(cj * CHUNK_CELLS);
	float size = CHUNK_CELLS * WORLD_CELL;
	int n = 0;
	for (int i = 0; i < count; i++)
		if (tufts[i].x >= x0 && tufts[i].x < x0 + size && tufts[i].z >= z0 && tufts[i].z < z0 + size)
			n++;
	c->tufts.len = 0;
	if (!n) return;

	dl_begin(&c->tufts, n * (3 + 8 * 36) + 64, vdesc_pnct_flush);
	rgbf grass = rgb_lerp(rgb_from(ALB_GRASS_AUTUMN), rgb_from(ALB_GRASS_SUMMER), 0.35f);
	for (int i = 0; i < count; i++) {
		const tuft_t *t = &tufts[i];
		if (!(t->x >= x0 && t->x < x0 + size && t->z >= z0 && t->z < z0 + size)) continue;
		float u0, v0, u1, v1;
		tuft_cell(t->kind, &u0, &v0, &u1, &v1);
		float w = t->size * (t->kind == TUFT_ROCK ? 1.0f : 1.1f);
		float h = t->size * (t->kind == TUFT_ROCK ? 1.0f : 0.95f);
		v3 nrm = world_normal(t->x, t->z);
		rgbf base = t->kind == TUFT_ROCK ? rgb_from(ALB_ROCK) : grass;
		u32 cb = rgba_u32(rgb_scale(base, 0.62f), 1.0f);
		u32 ct = rgba_u32(rgb_scale(base, 1.08f), 1.0f);
		GX_Begin(GX_QUADS, VF_3D, 8);
		for (int q = 0; q < 2; q++) {
			float a = t->yaw + q * (LL_PI * 0.5f);
			float dx = cosf(a) * w * 0.5f, dz = sinf(a) * w * 0.5f;
			v3 bl = v3_make(t->x - dx, t->y, t->z - dz), br = v3_make(t->x + dx, t->y, t->z + dz);
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

void terrain_build(void)
{
	build_grid();
	int ntufts;
	const tuft_t *tufts = world_tufts(&ntufts);
	for (int cj = 0; cj < CHUNKS; cj++) {
		for (int ci = 0; ci < CHUNKS; ci++) {
			record_ground(&s_chunks[cj][ci], ci, cj);
			record_tufts(&s_chunks[cj][ci], ci, cj, tufts, ntufts);
		}
	}
}

void terrain_draw(const daylight *d)
{
	gfx_model_identity();
	gfx_depth(true, true);
	gfx_blend_none();
	mat_lit_solid();
	mat_ramp_light(d->key_dir);
	vdesc_pnc_flush();
	for (int cj = 0; cj < CHUNKS; cj++)
		for (int ci = 0; ci < CHUNKS; ci++)
			if (gfx_sphere_visible(s_chunks[cj][ci].centre, s_chunks[cj][ci].radius))
				dl_draw(&s_chunks[cj][ci].ground);
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
			if (c->tufts.len && v3_len(v3_sub(c->centre, G.eye)) < 95.0f &&
			    gfx_sphere_visible(c->centre, c->radius))
				dl_draw(&c->tufts);
		}
}
