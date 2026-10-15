/*
 * foliage.c -- cut-paper trees.
 *
 * Each tree is three vertical planes crossing at 60 degrees. Every plane is
 * split down its middle, and the normals on each half lean outward, so the
 * light ramp paints one half lit and the other in shade: the two-tone
 * paper-cut look from the concept art, with no lighting hardware involved.
 *
 * One display list per tree kind (unit size); each instance supplies a model
 * matrix (position, yaw, scale and a small wind shear) and a konst tint.
 */
#include <math.h>
#include "gfx/foliage.h"
#include "gfx/gfx.h"
#include "gfx/mesh.h"
#include "world/world.h"
#include "assets.h"
#include "palette_data.h"

static dlist_t s_kind_dl[TREE_KINDS];
static float s_wind_time;

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
	case TREE_ASPEN: return rgb_lerp(rgb_from(ALB_ASPEN_SUMMER), rgb_from(ALB_ASPEN_AUTUMN), 0.25f);
	case TREE_SNAG: return rgb_from(ALB_BURN_SNAG);
	default: return rgb_lerp(rgb_from(ALB_PINE), rgb_from(ALB_PINE_DARK), 0.4f);
	}
}

static void record_kind(int kind)
{
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

	dl_begin(&s_kind_dl[kind], 3 * (3 + 8 * 36) + 64, vdesc_pnct_flush);
	for (int p = 0; p < 3; p++) {
		float a = p * (LL_PI / 3.0f);
		v3 d = v3_make(cosf(a) * 0.5f, 0, sinf(a) * 0.5f);
		v3 nl = v3_norm(v3_add(v3_scale(d, -2.0f), v3_scale(up, 0.25f)));
		v3 nr = v3_norm(v3_add(v3_scale(d, 2.0f), v3_scale(up, 0.25f)));
		v3 nc = v3_norm(v3_add(up, v3_make(0, 0.0f, 0)));
		v3 bl = v3_scale(d, -1.0f), br = d;
		v3 bc = v3_make(0, 0, 0);
		v3 tl = v3_add(bl, up), tr = v3_add(br, up), tc = up;
		GX_Begin(GX_QUADS, VF_3D, 8);
		/* left half */
		emit_pnct(bl, nl, c_bot, u0, v1);
		emit_pnct(bc, nc, c_mid_b, um, v1);
		emit_pnct(tc, nc, c_mid_t, um, v0);
		emit_pnct(tl, nl, c_top, u0, v0);
		/* right half */
		emit_pnct(bc, nc, c_mid_b, um, v1);
		emit_pnct(br, nr, c_bot, u1, v1);
		emit_pnct(tr, nr, c_top, u1, v0);
		emit_pnct(tc, nc, c_mid_t, um, v0);
		GX_End();
	}
	dl_end(&s_kind_dl[kind]);
}

void foliage_build(void)
{
	for (int k = 0; k < TREE_KINDS; k++)
		record_kind(k);
}

void foliage_update(float dt)
{
	s_wind_time += dt;
}

void foliage_draw(const daylight *d, float wind)
{
	int count;
	const tree_t *trees = world_trees(&count);
	gfx_depth(true, true);
	gfx_blend_none();
	mat_lit_cutout();
	vdesc_pnct_flush();

	for (int i = 0; i < count; i++) {
		const tree_t *t = &trees[i];
		v3 c = v3_make(t->x, t->y + t->height * 0.5f, t->z);
		if (!gfx_sphere_visible(c, t->height * 0.6f)) continue;

		/* Model = translate * shear(wind) * rotY(yaw) * scale(w, h, w) */
		float cy = cosf(t->yaw), sy = sinf(t->yaw);
		float sway = sinf(s_wind_time * 0.9f + t->phase) * 0.6f + sinf(s_wind_time * 2.3f + t->phase * 2.0f) * 0.25f;
		float shx = (0.010f + 0.012f * sway) * wind, shz = 0.006f * sway * wind;
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
		float k = 1.0f + 0.08f * t->tint;
		GX_SetTevKColor(GX_KCOLOR0, (GXColor){ (u8)clampf(240.0f * k, 0, 255),
		                                        (u8)clampf(245.0f * (1.0f + 0.04f * t->tint), 0, 255),
		                                        (u8)clampf(236.0f * k, 0, 255), 255 });
		dl_draw(&s_kind_dl[t->kind]);
	}
}
