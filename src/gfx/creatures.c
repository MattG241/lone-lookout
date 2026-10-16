/*
 * creatures.c -- the living things and passing events the story places in
 * the world: the doe and twin fawns below the drift fence (one small
 * folded-paper deer, drawn from a display list at three sizes) and the
 * Day 5 smoke column out on Harrow Ridge.
 *
 * Where and when they appear is the story's business (game/story.c); this
 * file only draws them.
 */
#include <math.h>
#include "gfx/creatures.h"
#include "gfx/gfx.h"
#include "gfx/mesh.h"
#include "game/story.h"
#include "core/noise.h"

static dlist_t s_deer;

#define SMOKE_PUFFS 36

/* A deer standing at the origin, facing north (-z), shoulder ~1 m. */
static void build_deer(void)
{
	u32 coat = rgba_u32(rgb_make(0.56f, 0.41f, 0.29f), 1.0f);
	u32 dark = rgba_u32(rgb_make(0.36f, 0.27f, 0.21f), 1.0f);
	u32 pale = rgba_u32(rgb_make(0.88f, 0.84f, 0.77f), 1.0f);
	mesh_box(v3_make(0, 0.95f, 0), v3_make(0.2f, 0.19f, 0.6f), coat);                 /* body */
	mesh_box(v3_make(0, 0.97f, 0.6f), v3_make(0.15f, 0.13f, 0.03f), pale);             /* rump */
	mesh_beam(v3_make(0, 1.0f, 0.62f), v3_make(0, 1.12f, 0.68f), 0.07f, 0.04f, dark);  /* tail */
	mesh_beam(v3_make(0, 1.02f, -0.45f), v3_make(0, 1.42f, -0.7f), 0.15f, 0.17f, coat); /* neck */
	mesh_box(v3_make(0, 1.47f, -0.82f), v3_make(0.085f, 0.085f, 0.16f), coat);        /* head */
	mesh_box(v3_make(0, 1.43f, -0.97f), v3_make(0.05f, 0.05f, 0.03f), dark);           /* muzzle */
	for (int s = -1; s <= 1; s += 2) {
		mesh_box(v3_make(s * 0.11f, 1.6f, -0.74f), v3_make(0.06f, 0.08f, 0.015f), dark); /* ears */
		for (int e = -1; e <= 1; e += 2) {
			float z = e * 0.46f;
			mesh_beam(v3_make(s * 0.12f, 0.82f, z), v3_make(s * 0.12f, 0.0f, z + e * 0.03f), 0.065f, 0.065f,
			          e > 0 ? coat : dark);                                         /* legs */
		}
	}
}

void creatures_build(void)
{
	dl_begin(&s_deer, 16 * MESH_BOX_BYTES + 256, vdesc_pnc_flush);
	build_deer();
	dl_end(&s_deer);
}

void creatures_draw(const daylight *d)
{
	bool begun = false;
	for (int i = 0; i < DEER_COUNT; i++) {
		v3 p;
		float b, alpha, s;
		if (!story_deer(i, &p, &b, &alpha, &s) || alpha < 0.35f) continue;
		if (!gfx_sphere_visible(v3_add(p, v3_make(0, s, 0)), 1.6f * s)) continue;
		float cb = cosf(b), sb = sinf(b);
		Mtx m = {
			{ s * cb, 0.0f, -s * sb, p.x },
			{ 0.0f, s, 0.0f, p.y },
			{ s * sb, 0.0f, s * cb, p.z },
		};
		if (!begun) {
			gfx_depth(true, true);
			gfx_blend_none();
			mat_lit_solid();
			begun = true;
		}
		gfx_model(m);
		v3 L = d->key_dir;
		mat_ramp_light(v3_make(cb * L.x + sb * L.z, L.y, -sb * L.x + cb * L.z));
		vdesc_pnc_flush();
		dl_draw(&s_deer);
	}
	if (begun) gfx_model_identity();
}

/* The column is drawn beyond the fog (it is 900 m out), so it carries its own
 * haze, and before the terrain, which then covers whatever is behind a hill. */
void smoke_draw(const daylight *d)
{
	v3 base;
	float height, alpha;
	if (!story_smoke(&base, &height, &alpha)) return;
	float t = G.frame / 60.0f;
	/* Grey-brown and dense low down, paler where it spreads; dimmed only by night. */
	float lum = 1.0f - 0.55f * d->night;
	rgbf low = rgb_scale(rgb_make(0.56f, 0.53f, 0.50f), lum), high = rgb_scale(rgb_make(0.80f, 0.79f, 0.77f), lum);
	v3 r = G.right, u = G.up;

	gfx_model_identity();
	gfx_fog_off();
	gfx_depth(true, false);
	gfx_blend_alpha();
	mat_vcolor_tex(&T_GLOW, true, false);
	vdesc_pct();
	gx_begin(GX_QUADS, VF_3D, SMOKE_PUFFS * 3 * 4);
	for (int i = 0; i < SMOKE_PUFFS; i++) {
		/* Each puff climbs the column and loops; higher up the wind leans it east
		 * and it spreads. */
		float k = fmodf((i + t * 0.08f) / SMOKE_PUFFS, 1.0f);
		float y = base.y + k * height * (1.0f - 0.12f * k * k);
		float lean = k * k * height * 0.35f;
		float wob = sinf(i * 2.3f + t * 0.2f) * (6.0f + 16.0f * k);
		float size = 22.0f + 60.0f * k;
		float a0 = alpha * 0.55f * (1.0f - 0.3f * k) * saturatef(k * 6.0f + 0.2f);
		/* A lumpy cluster of three, the side away from the light a shade darker. */
		for (int j = 0; j < 3; j++) {
			u32 h = hash32((u32)(i * 3 + j) * 2654435761u + 7u);
			float ox = (((h & 255) / 255.0f) - 0.5f) * size * 1.3f;
			float oy = ((((h >> 8) & 255) / 255.0f) - 0.5f) * size * 0.9f;
			float sz = size * (0.7f + 0.4f * ((h >> 16) & 255) / 255.0f);
			v3 c = v3_make(base.x + lean + wob + ox, y + oy, base.z + (wob + ox) * 0.5f);
			float shade = 0.82f + 0.18f * saturatef(0.5f + ox / size);
			rgbf col = rgb_scale(rgb_lerp(low, high, sqrtf(k)), shade);
			col = rgb_lerp(col, d->fog, 0.1f + 0.15f * k);
			GXColor gc = gx_col(col, a0);
			v3 rs = v3_scale(r, sz), us = v3_scale(u, sz);
			GX_Position3f32(c.x - rs.x + us.x, c.y - rs.y + us.y, c.z - rs.z + us.z);
			GX_Color4u8(gc.r, gc.g, gc.b, gc.a); GX_TexCoord2f32(0, 0);
			GX_Position3f32(c.x + rs.x + us.x, c.y + rs.y + us.y, c.z + rs.z + us.z);
			GX_Color4u8(gc.r, gc.g, gc.b, gc.a); GX_TexCoord2f32(1, 0);
			GX_Position3f32(c.x + rs.x - us.x, c.y + rs.y - us.y, c.z + rs.z - us.z);
			GX_Color4u8(gc.r, gc.g, gc.b, gc.a); GX_TexCoord2f32(1, 1);
			GX_Position3f32(c.x - rs.x - us.x, c.y - rs.y - us.y, c.z - rs.z - us.z);
			GX_Color4u8(gc.r, gc.g, gc.b, gc.a); GX_TexCoord2f32(0, 1);
		}
	}
	GX_End();
	gfx_blend_none();
	gfx_depth(true, true);
}
