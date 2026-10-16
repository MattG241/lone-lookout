/*
 * water.c -- Mirror Tarn's surface.
 *
 * A level grid over the tarn, coloured per vertex every frame: dark,
 * see-through water looking down; sky colour at grazing angles (a cheap
 * Fresnel term); and a soft path of light toward a low sun. The shore
 * terrain hides the grid wherever the ground rises above the water.
 */
#include <math.h>
#include "gfx/water.h"
#include "gfx/gfx.h"
#include "world/world.h"
#include "palette_data.h"

static int s_i0, s_j0, s_i1, s_j1;       /* grid vertex range covering the tarn */
static float s_time;

void water_build(void)
{
	s_i0 = s_j0 = 1 << 20;
	s_i1 = s_j1 = -1;
	for (int j = 0; j < WORLD_VERTS; j++)
		for (int i = 0; i < WORLD_VERTS; i++) {
			float x = i * WORLD_CELL, z = j * WORLD_CELL;
			float dx = x - TARN_X, dz = z - TARN_Z;
			if (dx * dx + dz * dz > TARN_RADIUS * TARN_RADIUS) continue;
			if (world_grid_height(i, j) > TARN_WATER_Y) continue;
			if (i < s_i0) s_i0 = i;
			if (j < s_j0) s_j0 = j;
			if (i > s_i1) s_i1 = i;
			if (j > s_j1) s_j1 = j;
		}
	/* One cell of margin so the shoreline always meets the ground. */
	s_i0 = s_i0 > 0 ? s_i0 - 1 : 0;
	s_j0 = s_j0 > 0 ? s_j0 - 1 : 0;
	s_i1 = s_i1 < WORLD_GRID ? s_i1 + 1 : WORLD_GRID;
	s_j1 = s_j1 < WORLD_GRID ? s_j1 + 1 : WORLD_GRID;
}

void water_update(float dt) { s_time += dt; }

static GXColor water_col(const daylight *d, rgbf deep, rgbf sky, float x, float z)
{
	v3 v = v3_norm(v3_sub(v3_make(x, TARN_WATER_Y, z), G.eye));
	float c = fabsf(v.y);
	float f = 0.05f + 0.95f * powf(1.0f - c, 4.0f);
	f *= 0.9f + 0.1f * sinf(s_time * 0.7f + x * 0.31f + z * 0.23f) * sinf(s_time * 0.45f - x * 0.17f + z * 0.29f);
	rgbf col = rgb_lerp(deep, sky, saturatef(f));
	/* Light on the water toward a low sun. */
	v3 r = v3_make(v.x, -v.y, v.z);
	float s = fmaxf(0.0f, v3_dot(r, d->sun_dir));
	float glint = powf(s, 24.0f) * d->sun_vis * (1.0f - saturatef(d->sun_dir.y * 1.5f));
	col = rgb_add(col, rgb_scale(d->sun, glint * 0.6f));
	return gx_col(col, 0.6f + 0.35f * saturatef(f) + glint * 0.2f);
}

void water_draw(const daylight *d)
{
	if (s_i1 < s_i0) return;
	float cx = (s_i0 + s_i1) * 0.5f * WORLD_CELL, cz = (s_j0 + s_j1) * 0.5f * WORLD_CELL;
	float r = (s_i1 - s_i0 + s_j1 - s_j0) * 0.5f * WORLD_CELL;
	if (!gfx_sphere_visible(v3_make(cx, TARN_WATER_Y, cz), r)) return;

	rgbf light = rgb_add(d->ambient, rgb_scale(d->sun, d->key_strength * 0.35f));
	rgbf deep = rgb_mul(rgb_scale(rgb_from(ALB_WATER), 0.42f), light);
	rgbf sky = rgb_scale(rgb_lerp(d->sky_horizon, d->sky_top, 0.3f), 0.93f);

	gfx_model_identity();
	gfx_depth(true, false);
	gfx_blend_alpha();
	mat_vcolor();
	vdesc_pc();
	for (int j = s_j0; j < s_j1; j++) {
		gx_begin(GX_TRIANGLESTRIP, VF_3D, (s_i1 - s_i0 + 1) * 2);
		for (int i = s_i0; i <= s_i1; i++) {
			for (int k = 0; k < 2; k++) {
				float x = i * WORLD_CELL, z = (j + k) * WORLD_CELL;
				GXColor c = water_col(d, deep, sky, x, z);
				GX_Position3f32(x, TARN_WATER_Y, z);
				GX_Color4u8(c.r, c.g, c.b, c.a);
			}
		}
		GX_End();
	}
	gfx_blend_none();
	gfx_depth(true, true);
}
