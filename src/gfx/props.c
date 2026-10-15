/*
 * props.c -- the Kestrel Lookout blockout, built from beams and boxes in code:
 * four splayed legs with girts and cross-bracing, the deck and railing, a
 * glazed cab with a pyramid roof, one long stair flight, Augie's note on the
 * bottom post, and the fallen-log bench. Everything goes into one display
 * list in world space.
 */
#include <math.h>
#include "gfx/props.h"
#include "gfx/gfx.h"
#include "gfx/mesh.h"
#include "world/world.h"
#include "palette_data.h"

static dlist_t s_props;

static u32 wood(float k)
{
	return rgba_u32(rgb_scale(rgb_from(ALB_TOWER_WOOD), k), 1.0f);
}

static v3 leg_point(int corner, float y_frac, float y0)
{
	float sx = (corner & 1) ? 1.0f : -1.0f, sz = (corner & 2) ? 1.0f : -1.0f;
	float spread = lerpf(TOWER_LEG_BASE, TOWER_LEG_TOP, y_frac);
	return v3_make(TOWER_X + sx * spread, y0 + y_frac * TOWER_DECK_Y, TOWER_Z + sz * spread);
}

static void build_tower(float y0)
{
	/* Corners in perimeter order so neighbours share a face. */
	static const int ring[4] = { 0, 1, 3, 2 };
	u32 leg = wood(0.78f), brace = wood(0.9f), deck = wood(1.1f), rail = wood(1.0f);

	for (int c = 0; c < 4; c++)
		mesh_beam(v3_sub(leg_point(c, 0.0f, y0), v3_make(0, 0.4f, 0)), leg_point(c, 1.0f, y0), 0.26f, 0.26f, leg);

	/* Girts at thirds, X-braces between them on every face. */
	const float levels[4] = { 0.02f, 1.0f / 3.0f, 2.0f / 3.0f, 0.98f };
	for (int f = 0; f < 4; f++) {
		int a = ring[f], b = ring[(f + 1) & 3];
		for (int l = 1; l < 4; l++)
			mesh_beam(leg_point(a, levels[l], y0), leg_point(b, levels[l], y0), 0.12f, 0.16f, brace);
		for (int l = 0; l < 3; l++) {
			mesh_beam(leg_point(a, levels[l], y0), leg_point(b, levels[l + 1], y0), 0.08f, 0.1f, brace);
			mesh_beam(leg_point(b, levels[l], y0), leg_point(a, levels[l + 1], y0), 0.08f, 0.1f, brace);
		}
	}

	/* Deck */
	float dy = y0 + TOWER_DECK_Y;
	mesh_box(v3_make(TOWER_X, dy - 0.09f, TOWER_Z), v3_make(TOWER_DECK_HALF, 0.09f, TOWER_DECK_HALF), deck);

	/* Railing: posts and a top rail, open at the stair head (south side). */
	float e = TOWER_DECK_HALF - 0.05f;
	for (int i = 0; i < 5; i++) {
		float t = -e + 2.0f * e * i / 4.0f;
		v3 posts[4] = { { t, 0, -e }, { t, 0, e }, { -e, 0, t }, { e, 0, t } };
		for (int k = 0; k < 4; k++) {
			if (k == 1 && fabsf(t) < STAIR_HALF_W + 0.1f) continue;
			v3 p = v3_make(TOWER_X + posts[k].x, dy, TOWER_Z + posts[k].z);
			mesh_beam(p, v3_add(p, v3_make(0, 1.0f, 0)), 0.08f, 0.08f, rail);
		}
	}
	float ry = dy + 1.0f;
	mesh_beam(v3_make(TOWER_X - e, ry, TOWER_Z - e), v3_make(TOWER_X + e, ry, TOWER_Z - e), 0.08f, 0.1f, rail);
	mesh_beam(v3_make(TOWER_X - e, ry, TOWER_Z - e), v3_make(TOWER_X - e, ry, TOWER_Z + e), 0.08f, 0.1f, rail);
	mesh_beam(v3_make(TOWER_X + e, ry, TOWER_Z - e), v3_make(TOWER_X + e, ry, TOWER_Z + e), 0.08f, 0.1f, rail);
	mesh_beam(v3_make(TOWER_X - e, ry, TOWER_Z + e), v3_make(STAIR_X - STAIR_HALF_W - 0.1f, ry, TOWER_Z + e), 0.08f, 0.1f, rail);
	mesh_beam(v3_make(STAIR_X + STAIR_HALF_W + 0.1f, ry, TOWER_Z + e), v3_make(TOWER_X + e, ry, TOWER_Z + e), 0.08f, 0.1f, rail);

	/* Cab: lower wall, glazing, corner posts and mullions, header, roof. */
	float ch = TOWER_CAB_HALF;
	mesh_box(v3_make(TOWER_X, dy + 0.5f, TOWER_Z), v3_make(ch, 0.5f, ch), wood(0.95f));
	/* The glazing is drawn per frame (props_draw) so it can glow at night. */
	for (int c = 0; c < 4; c++) {
		float sx = (c & 1) ? 1.0f : -1.0f, sz = (c & 2) ? 1.0f : -1.0f;
		v3 b = v3_make(TOWER_X + sx * ch, dy + 1.0f, TOWER_Z + sz * ch);
		mesh_beam(b, v3_add(b, v3_make(0, 1.35f, 0)), 0.14f, 0.14f, wood(0.9f));
	}
	for (int f = 0; f < 4; f++) {
		for (int m = -1; m <= 1; m += 2) {
			float o = m * ch / 3.0f;
			v3 b = f < 2 ? v3_make(TOWER_X + o, dy + 1.0f, TOWER_Z + (f ? ch : -ch))
			             : v3_make(TOWER_X + (f == 3 ? ch : -ch), dy + 1.0f, TOWER_Z + o);
			mesh_beam(b, v3_add(b, v3_make(0, 1.3f, 0)), 0.07f, 0.07f, wood(0.9f));
		}
	}
	mesh_box(v3_make(TOWER_X, dy + 2.4f, TOWER_Z), v3_make(ch + 0.02f, 0.1f, ch + 0.02f), wood(0.95f));

	u32 roof = rgba_u32(rgb_make(0.47f, 0.44f, 0.42f), 1.0f);
	float rh = dy + 2.5f, apex_y = rh + 1.15f, o = ch + 0.55f;
	v3 apex = v3_make(TOWER_X, apex_y, TOWER_Z);
	v3 rc[4] = { { TOWER_X - o, rh, TOWER_Z - o }, { TOWER_X + o, rh, TOWER_Z - o },
	             { TOWER_X + o, rh, TOWER_Z + o }, { TOWER_X - o, rh, TOWER_Z + o } };
	for (int i = 0; i < 4; i++) {
		mesh_tri(rc[i], rc[(i + 1) & 3], apex, roof);
		mesh_tri(rc[(i + 1) & 3], rc[i], v3_make(TOWER_X, rh - 0.05f, TOWER_Z), roof);  /* soffit */
	}
}

static void build_stairs(float y0)
{
	float zt = TOWER_Z + TOWER_DECK_HALF, zb = zt + STAIR_RUN;
	const int steps = 36;
	u32 stringer = wood(0.8f), tread = wood(1.05f), rail = wood(0.95f);
	for (int s = -1; s <= 1; s += 2) {
		float x = STAIR_X + s * (STAIR_HALF_W + 0.03f);
		mesh_beam(v3_make(x, y0 - 0.15f, zb + 0.2f), v3_make(x, y0 + TOWER_DECK_Y - 0.15f, zt), 0.08f, 0.3f, stringer);
		float xr = STAIR_X + s * (STAIR_HALF_W + 0.08f);
		mesh_beam(v3_make(xr, y0 + 1.0f, zb), v3_make(xr, y0 + TOWER_DECK_Y + 1.0f, zt), 0.07f, 0.07f, rail);
		for (int p = 0; p <= 4; p++) {
			float f = p / 4.0f;
			v3 b = v3_make(xr, y0 + f * TOWER_DECK_Y, zb - f * STAIR_RUN);
			mesh_beam(b, v3_add(b, v3_make(0, (p == 0 ? 1.25f : 1.0f), 0)), 0.09f, 0.09f, rail);
		}
	}
	for (int i = 0; i < steps; i++) {
		float z = zb - (i + 0.5f) * STAIR_RUN / steps;
		float y = y0 + (i + 1) * TOWER_DECK_Y / steps;
		mesh_box(v3_make(STAIR_X, y - 0.03f, z), v3_make(STAIR_HALF_W, 0.03f, STAIR_RUN / steps * 0.5f + 0.03f), tread);
	}
	/* Augie's note: a small sheet pinned to the bottom right-hand post. */
	u32 paper = rgba_u32(rgb_make(0.93f, 0.90f, 0.82f), 1.0f);
	mesh_box(v3_make(STAIR_X + STAIR_HALF_W + 0.08f, y0 + 1.2f, zb + 0.055f), v3_make(0.09f, 0.12f, 0.004f), paper);
}

static void build_bench(void)
{
	v3 b = world_bench_pos();
	u32 bark = rgba_u32(rgb_make(0.42f, 0.34f, 0.27f), 1.0f);
	u32 cut = rgba_u32(rgb_make(0.70f, 0.58f, 0.42f), 1.0f);
	const int sides = 8;
	float r = 0.24f, half = 1.2f, cy = b.y + 0.22f;
	/* Octagonal log lying north-south, ends showing the pale cut wood. */
	for (int i = 0; i < sides; i++) {
		float a0 = LL_TAU * i / sides, a1 = LL_TAU * (i + 1) / sides;
		v3 p0 = v3_make(b.x + cosf(a0) * r, cy + sinf(a0) * r, b.z - half);
		v3 p1 = v3_make(b.x + cosf(a1) * r, cy + sinf(a1) * r, b.z - half);
		v3 q0 = v3_make(p0.x, p0.y, b.z + half), q1 = v3_make(p1.x, p1.y, b.z + half);
		mesh_tri(p0, q0, q1, bark);
		mesh_tri(p0, q1, p1, bark);
		mesh_tri(v3_make(b.x, cy, b.z - half), p1, p0, cut);
		mesh_tri(v3_make(b.x, cy, b.z + half), q0, q1, cut);
	}
	mesh_beam(v3_make(b.x, cy + 0.1f, b.z + 0.3f), v3_make(b.x - 0.25f, cy + 0.45f, b.z + 0.45f), 0.07f, 0.07f, bark);
}

void props_build(void)
{
	float y0 = world_pad_height();
	dl_begin(&s_props, 420 * MESH_BOX_BYTES, vdesc_pnc_flush);
	build_tower(y0);
	build_stairs(y0);
	build_bench();
	dl_end(&s_props);
}

static void draw_glass(const daylight *d)
{
	/* Daytime: the panes reflect the sky. Night: the lamp is lit inside. */
	rgbf day = rgb_scale(rgb_lerp(d->sky_horizon, d->sky_top, 0.4f), 0.92f);
	rgbf lamp = rgb_scale(rgb_from(ALB_WINDOW_LIGHT), 1.1f);
	rgbf c = rgb_lerp(day, lamp, d->window_glow);
	float dy = world_pad_height() + TOWER_DECK_Y, ch = TOWER_CAB_HALF;
	gfx_model_identity();
	mat_vcolor();
	GX_SetCullMode(GX_CULL_FRONT);
	vdesc_pnc();
	mesh_box(v3_make(TOWER_X, dy + 1.65f, TOWER_Z), v3_make(ch - 0.05f, 0.65f, ch - 0.05f), rgba_u32(c, 1.0f));

	/* A soft halo around the lit cab, visible from across the bowl. */
	if (d->window_glow > 0.05f) {
		v3 cpos = v3_make(TOWER_X, dy + 1.65f, TOWER_Z);
		float size = 4.5f;
		v3 r = v3_scale(G.right, size), u = v3_scale(G.up, size);
		GXColor gc = gx_col(rgb_from(ALB_WINDOW_LIGHT), 0.55f * d->window_glow);
		gfx_depth(true, false);
		gfx_blend_add();
		mat_vcolor_tex(&T_GLOW, true, false);
		vdesc_pct();
		GX_Begin(GX_QUADS, VF_3D, 4);
		GX_Position3f32(cpos.x - r.x + u.x, cpos.y - r.y + u.y, cpos.z - r.z + u.z);
		GX_Color4u8(gc.r, gc.g, gc.b, gc.a); GX_TexCoord2f32(0, 0);
		GX_Position3f32(cpos.x + r.x + u.x, cpos.y + r.y + u.y, cpos.z + r.z + u.z);
		GX_Color4u8(gc.r, gc.g, gc.b, gc.a); GX_TexCoord2f32(1, 0);
		GX_Position3f32(cpos.x + r.x - u.x, cpos.y + r.y - u.y, cpos.z + r.z - u.z);
		GX_Color4u8(gc.r, gc.g, gc.b, gc.a); GX_TexCoord2f32(1, 1);
		GX_Position3f32(cpos.x - r.x - u.x, cpos.y - r.y - u.y, cpos.z - r.z - u.z);
		GX_Color4u8(gc.r, gc.g, gc.b, gc.a); GX_TexCoord2f32(0, 1);
		GX_End();
		gfx_blend_none();
		gfx_depth(true, true);
	}
}

void props_draw(const daylight *d)
{
	gfx_model_identity();
	gfx_depth(true, true);
	gfx_blend_none();
	mat_lit_solid();
	mat_ramp_light(d->key_dir);
	vdesc_pnc_flush();
	dl_draw(&s_props);
	draw_glass(d);
}
