/*
 * foliage.c -- cut-paper trees.
 *
 * Each tree is three vertical planes crossing at 60 degrees. Every plane is
 * split down its middle, and the normals on each half lean outward, so the
 * light ramp paints one half lit and the other in shade: the two-tone
 * paper-cut look from the concept art, with no lighting hardware involved.
 *
 * Near the camera (chunks within NEAR_RANGE) every tree is its own draw: a
 * unit-tree display list per kind and a model matrix carrying position, yaw,
 * scale and a little wind shear. Further out, each 64 m chunk's trees are
 * pre-transformed into one static display list -- no wind at that distance,
 * and one call per chunk instead of one per tree. Beyond FAR_RANGE a chunk
 * draws a lighter list: two whole planes at right angles (4 triangles, not
 * 12), lit and shaded edge to edge, so the ramp still splits them in two.
 */
#include <malloc.h>
#include <math.h>
#include "gfx/foliage.h"
#include "gfx/gfx.h"
#include "gfx/mesh.h"
#include "world/world.h"
#include "assets.h"
#include "palette_data.h"

#define CHUNKS 8
#define CHUNK_SIZE (WORLD_SIZE / CHUNKS)
#define NEAR_RANGE 70.0f
#define FAR_RANGE 150.0f
#define UNIT_VERTS 24                 /* 3 planes x 2 halves x 4 */

typedef struct {
	v3 p[UNIT_VERTS], n[UNIT_VERTS];
	u32 c[UNIT_VERTS];
	float s[UNIT_VERTS], t[UNIT_VERTS];
} unit_tree;

typedef struct {
	dlist_t far, far2;
	v3 centre;
	float radius;
	float x0, z0;
	int first, count;
} fchunk_t;

static unit_tree s_unit[TREE_KINDS];
static dlist_t s_kind_dl[TREE_KINDS];
static fchunk_t s_chunk[CHUNKS][CHUNKS];
static int *s_order;
static float s_wind_time, s_season;

static void kind_cell(int kind, int *cell)
{
	static const int cells[TREE_KINDS][4] = {
		{ ATLAS_CONIFER0 }, { ATLAS_CONIFER1 }, { ATLAS_CONIFER2 }, { ATLAS_CONIFER3 },
		{ ATLAS_ASPEN }, { ATLAS_SNAG },
	};
	for (int i = 0; i < 4; i++) cell[i] = cells[kind][i];
}

static rgbf kind_albedo(int kind)
{
	switch (kind) {
	case TREE_ASPEN:
		return rgb_lerp(rgb_from(ALB_ASPEN_SUMMER), rgb_from(ALB_ASPEN_AUTUMN), saturatef(0.1f + s_season * 0.9f));
	case TREE_SNAG: return rgb_scale(rgb_from(ALB_BURN_SNAG), 1.12f);
	default: return rgb_lerp(rgb_from(ALB_PINE), rgb_from(ALB_PINE_DARK), 0.4f);
	}
}

static void build_unit(int kind)
{
	unit_tree *u = &s_unit[kind];
	int cell[4];
	kind_cell(kind, cell);
	float u0 = (cell[0] + 0.5f) / TEX_FOLIAGE_W, u1 = (cell[0] + cell[2] - 0.5f) / TEX_FOLIAGE_W;
	float v0 = (cell[1] + 0.5f) / TEX_FOLIAGE_H, v1 = (cell[1] + cell[3] - 0.5f) / TEX_FOLIAGE_H;
	float um = (u0 + u1) * 0.5f;
	rgbf alb = kind_albedo(kind);
	u32 c_bot = rgba_u32(rgb_scale(alb, 0.62f), 1.0f);
	u32 c_top = rgba_u32(rgb_scale(alb, 1.05f), 1.0f);
	u32 c_mid_b = rgba_u32(rgb_scale(alb, 0.70f), 1.0f);
	u32 c_mid_t = rgba_u32(rgb_scale(alb, 1.12f), 1.0f);
	v3 up = v3_make(0, 1, 0);
	int k = 0;
#define V(P, N, C, S, T) do { u->p[k] = (P); u->n[k] = (N); u->c[k] = (C); u->s[k] = (S); u->t[k] = (T); k++; } while (0)
	for (int pl = 0; pl < 3; pl++) {
		float a = pl * (LL_PI / 3.0f);
		v3 d = v3_make(cosf(a) * 0.5f, 0, sinf(a) * 0.5f);
		v3 nl = v3_norm(v3_add(v3_scale(d, -2.0f), v3_scale(up, 0.25f)));
		v3 nr = v3_norm(v3_add(v3_scale(d, 2.0f), v3_scale(up, 0.25f)));
		v3 nc = up;
		v3 bl = v3_scale(d, -1.0f), br = d, bc = v3_make(0, 0, 0);
		v3 tl = v3_add(bl, up), tr = v3_add(br, up), tc = up;
		V(bl, nl, c_bot, u0, v1);  V(bc, nc, c_mid_b, um, v1);  V(tc, nc, c_mid_t, um, v0);  V(tl, nl, c_top, u0, v0);
		V(bc, nc, c_mid_b, um, v1);  V(br, nr, c_bot, u1, v1);  V(tr, nr, c_top, u1, v0);  V(tc, nc, c_mid_t, um, v0);
	}
#undef V
}

static void record_kind(int kind, bool again)
{
	unit_tree *u = &s_unit[kind];
	if (again) dl_rebegin(&s_kind_dl[kind], vdesc_pnct_flush);
	else dl_begin(&s_kind_dl[kind], 3 * (3 + 8 * 36) + 64, vdesc_pnct_flush);
	for (int pl = 0; pl < 3; pl++) {
		GX_Begin(GX_QUADS, VF_3D, 8);
		for (int k = pl * 8; k < pl * 8 + 8; k++) emit_pnct(u->p[k], u->n[k], u->c[k], u->s[k], u->t[k]);
		GX_End();
	}
	dl_end(&s_kind_dl[kind]);
}

/* The per-tree tint, as the near path applies it through konst colour K0. */
static GXColor tree_tint(const tree_t *t)
{
	float k = 1.0f + 0.08f * t->tint;
	return (GXColor){ (u8)clampf(240.0f * k, 0, 255), (u8)clampf(245.0f * (1.0f + 0.04f * t->tint), 0, 255),
	                  (u8)clampf(236.0f * k, 0, 255), 255 };
}

static u32 tint_rgba(u32 c, GXColor k)
{
	u32 r = ((c >> 24) & 0xFF) * k.r / 255, g = ((c >> 16) & 0xFF) * k.g / 255, b = ((c >> 8) & 0xFF) * k.b / 255;
	return (r << 24) | (g << 16) | (b << 8) | 0xFF;
}

static void record_far(fchunk_t *fc, bool again)
{
	int n;
	const tree_t *trees = world_trees(&n);
	if (!fc->count) {
		fc->far.len = 0;
		return;
	}
	if (again) dl_rebegin(&fc->far, vdesc_pnct_flush);
	else dl_begin(&fc->far, fc->count * 3 * (3 + 8 * 36) + 64, vdesc_pnct_flush);
	for (int i = 0; i < fc->count; i++) {
		const tree_t *t = &trees[s_order[fc->first + i]];
		const unit_tree *u = &s_unit[t->kind];
		float cy = cosf(t->yaw), sy = sinf(t->yaw), w = t->width, h = t->height;
		GXColor tint = tree_tint(t);
		for (int pl = 0; pl < 3; pl++) {
			GX_Begin(GX_QUADS, VF_3D, 8);
			for (int k = pl * 8; k < pl * 8 + 8; k++) {
				v3 p = u->p[k], nn = u->n[k];
				v3 wp = v3_make(cy * w * p.x + sy * w * p.z + t->x, h * p.y + t->y, -sy * w * p.x + cy * w * p.z + t->z);
				v3 wn = v3_make(cy * nn.x + sy * nn.z, nn.y, -sy * nn.x + cy * nn.z);
				emit_pnct(wp, wn, tint_rgba(u->c[k], tint), u->s[k], u->t[k]);
			}
			GX_End();
		}
	}
	dl_end(&fc->far);
}

/* The distant tree: two planes crossing at right angles, one quad each. */
static void record_far2(fchunk_t *fc, bool again)
{
	int n;
	const tree_t *trees = world_trees(&n);
	if (!fc->count) {
		fc->far2.len = 0;
		return;
	}
	if (again) dl_rebegin(&fc->far2, vdesc_pnct_flush);
	else dl_begin(&fc->far2, fc->count * (3 + 8 * 36) + 64, vdesc_pnct_flush);
	v3 up = v3_make(0, 1, 0);
	for (int i = 0; i < fc->count; i++) {
		const tree_t *t = &trees[s_order[fc->first + i]];
		const unit_tree *u = &s_unit[t->kind];
		GXColor tint = tree_tint(t);
		/* The unit tree's corners: u0/u1 at its first plane's ends, v0/v1 top and bottom. */
		float u0 = u->s[0], u1 = u->s[5], v0 = u->t[3], v1 = u->t[0];
		u32 cb = tint_rgba(u->c[0], tint), ct = tint_rgba(u->c[3], tint);
		GX_Begin(GX_QUADS, VF_3D, 8);
		for (int pl = 0; pl < 2; pl++) {
			float a = t->yaw + pl * (LL_PI * 0.5f);
			v3 d = v3_make(cosf(a) * 0.5f * t->width, 0, sinf(a) * 0.5f * t->width);
			v3 dn = v3_norm(d);
			v3 nl = v3_norm(v3_add(v3_scale(dn, -1.0f), v3_scale(up, 0.25f)));
			v3 nr = v3_norm(v3_add(dn, v3_scale(up, 0.25f)));
			v3 base = v3_make(t->x, t->y, t->z);
			v3 bl = v3_sub(base, d), br = v3_add(base, d);
			v3 tl = v3_add(bl, v3_make(0, t->height, 0)), tr = v3_add(br, v3_make(0, t->height, 0));
			emit_pnct(bl, nl, cb, u0, v1);
			emit_pnct(br, nr, cb, u1, v1);
			emit_pnct(tr, nr, ct, u1, v0);
			emit_pnct(tl, nl, ct, u0, v0);
		}
		GX_End();
	}
	dl_end(&fc->far2);
}

static void sort_trees(void)
{
	int n;
	const tree_t *trees = world_trees(&n);
	s_order = malloc(sizeof(int) * (n ? n : 1));
	int fill = 0;
	for (int cj = 0; cj < CHUNKS; cj++)
		for (int ci = 0; ci < CHUNKS; ci++) {
			fchunk_t *fc = &s_chunk[cj][ci];
			fc->first = fill;
			fc->x0 = ci * CHUNK_SIZE;
			fc->z0 = cj * CHUNK_SIZE;
			float ymin = 1e9f, ymax = -1e9f;
			for (int i = 0; i < n; i++) {
				int ti = (int)(trees[i].x / CHUNK_SIZE), tj = (int)(trees[i].z / CHUNK_SIZE);
				ti = ti < 0 ? 0 : (ti >= CHUNKS ? CHUNKS - 1 : ti);
				tj = tj < 0 ? 0 : (tj >= CHUNKS ? CHUNKS - 1 : tj);
				if (ti != ci || tj != cj) continue;
				s_order[fill++] = i;
				ymin = fminf(ymin, trees[i].y);
				ymax = fmaxf(ymax, trees[i].y + trees[i].height);
			}
			fc->count = fill - fc->first;
			if (!fc->count) ymin = ymax = 0.0f;
			fc->centre = v3_make(fc->x0 + CHUNK_SIZE * 0.5f, (ymin + ymax) * 0.5f, fc->z0 + CHUNK_SIZE * 0.5f);
			float hh = (ymax - ymin) * 0.5f + 8.0f;
			fc->radius = sqrtf(CHUNK_SIZE * CHUNK_SIZE * 0.5f + hh * hh) + 8.0f;
		}
}

void foliage_build(float season)
{
	s_season = season;
	for (int k = 0; k < TREE_KINDS; k++) {
		build_unit(k);
		record_kind(k, false);
	}
	sort_trees();
	for (int cj = 0; cj < CHUNKS; cj++)
		for (int ci = 0; ci < CHUNKS; ci++) {
			record_far(&s_chunk[cj][ci], false);
			record_far2(&s_chunk[cj][ci], false);
		}
}

void foliage_set_season(float season)
{
	s_season = season;
	for (int k = 0; k < TREE_KINDS; k++) {
		build_unit(k);
		record_kind(k, true);
	}
	for (int cj = 0; cj < CHUNKS; cj++)
		for (int ci = 0; ci < CHUNKS; ci++) {
			record_far(&s_chunk[cj][ci], true);
			record_far2(&s_chunk[cj][ci], true);
		}
}

void foliage_update(float dt)
{
	s_wind_time += dt;
}

static float chunk_dist(const fchunk_t *c)
{
	float dx = fmaxf(fmaxf(c->x0 - G.eye.x, 0.0f), G.eye.x - (c->x0 + CHUNK_SIZE));
	float dz = fmaxf(fmaxf(c->z0 - G.eye.z, 0.0f), G.eye.z - (c->z0 + CHUNK_SIZE));
	return sqrtf(dx * dx + dz * dz);
}

int foliage_draw(const daylight *d, float wind)
{
	int n, drawn = 0;
	const tree_t *trees = world_trees(&n);
	gfx_depth(true, true);
	gfx_blend_none();
	mat_lit_cutout(false);                     /* the far lists have their tints baked in */
	vdesc_pnct_flush();

	/* Far chunks: static world-space lists. */
	gfx_model_identity();
	mat_ramp_light(d->key_dir);
	for (int cj = 0; cj < CHUNKS; cj++)
		for (int ci = 0; ci < CHUNKS; ci++) {
			fchunk_t *fc = &s_chunk[cj][ci];
			float dist = chunk_dist(fc);
			if (fc->count && dist >= NEAR_RANGE && gfx_sphere_visible(fc->centre, fc->radius)) {
				dl_draw(dist < FAR_RANGE ? &fc->far : &fc->far2);
				drawn += fc->count;
			}
		}

	/* Near chunks: one draw per tree, swaying, tinted through K0. */
	mat_lit_cutout(true);
	for (int cj = 0; cj < CHUNKS; cj++)
		for (int ci = 0; ci < CHUNKS; ci++) {
			fchunk_t *fc = &s_chunk[cj][ci];
			if (!fc->count || chunk_dist(fc) >= NEAR_RANGE || !gfx_sphere_visible(fc->centre, fc->radius)) continue;
			for (int i = 0; i < fc->count; i++) {
				const tree_t *t = &trees[s_order[fc->first + i]];
				v3 c = v3_make(t->x, t->y + t->height * 0.5f, t->z);
				if (!gfx_sphere_visible(c, t->height * 0.6f)) continue;
				/* Model = translate * shear(wind) * rotY(yaw) * scale(w, h, w) */
				float cy = cosf(t->yaw), sy = sinf(t->yaw);
				float sway = sinf(s_wind_time * 0.9f + t->phase) * 0.6f + sinf(s_wind_time * 2.3f + t->phase * 2.0f) * 0.25f;
				float bend = t->kind == TREE_SNAG ? 0.3f : 1.0f;
				float shx = (0.010f + 0.012f * sway) * wind * bend, shz = 0.006f * sway * wind * bend;
				float w = t->width, h = t->height;
				Mtx m = {
					{ cy * w, shx * h, sy * w, t->x },
					{ 0.0f, h, 0.0f, t->y },
					{ -sy * w, shz * h, cy * w, t->z },
				};
				gfx_model(m);
				/* Key light in model space: rotate by -yaw (scale/shear ignored). */
				v3 L = d->key_dir;
				mat_ramp_light(v3_norm(v3_make(cy * L.x - sy * L.z, L.y, sy * L.x + cy * L.z)));
				GX_SetTevKColor(GX_KCOLOR0, tree_tint(t));
				dl_draw(&s_kind_dl[t->kind]);
				drawn++;
			}
		}
	gfx_model_identity();
	return drawn;
}
