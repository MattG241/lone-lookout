/*
 * props.c -- draws the hand-built world: the prop groups recorded by
 * props_tower.c and props_places.c, the interiors with lamp light, and the
 * glazing (drawn per frame so it can show the sky by day and the lamp by
 * night, and so it is invisible from inside).
 */
#include <math.h>
#include "gfx/props.h"
#include "gfx/props_int.h"
#include "gfx/gfx.h"

typedef struct {
	dlist_t dl;
	v3 centre;
	float radius;
} group_t;

static group_t s_groups[PG_COUNT];

void pg_begin(prop_group g, int boxes)
{
	dl_begin(&s_groups[g].dl, boxes * MESH_BOX_BYTES + 256, vdesc_pnc_flush);
}

void pg_end(prop_group g, v3 centre, float radius)
{
	dl_end(&s_groups[g].dl);
	s_groups[g].centre = centre;
	s_groups[g].radius = radius;
}

void props_build(void)
{
	build_tower_props();
	build_place_props();
}

/* How far off a group is still drawn (0: always). Interiors only show through
 * the windows up close, and the smaller sites are a few pixels past this. */
static const float DRAW_RANGE[PG_COUNT] = {
	[PG_TOWER_IN] = 45.0f, [PG_CABIN_IN] = 30.0f, [PG_BINOCULARS] = 30.0f, [PG_TARN] = 200.0f,
	[PG_RIDGE] = 200.0f, [PG_MEADOW] = 200.0f, [PG_SPRING] = 120.0f, [PG_BURN] = 200.0f, [PG_TRAILHEAD] = 200.0f,
};

static bool visible(prop_group g)
{
	const group_t *gr = &s_groups[g];
	if (!gr->dl.len) return false;
	if (DRAW_RANGE[g] > 0.0f && v3_len(v3_sub(gr->centre, G.eye)) - gr->radius > DRAW_RANGE[g]) return false;
	return gfx_sphere_visible(gr->centre, gr->radius);
}

/* Glazing faces wind so their front faces outward: invisible from inside. */
static void glass_quad(v3 a, v3 b, float y0, float y1, GXColor c)
{
	GX_Begin(GX_QUADS, VF_3D, 4);
	GX_Position3f32(a.x, y0, a.z); GX_Color4u8(c.r, c.g, c.b, c.a);
	GX_Position3f32(b.x, y0, b.z); GX_Color4u8(c.r, c.g, c.b, c.a);
	GX_Position3f32(b.x, y1, b.z); GX_Color4u8(c.r, c.g, c.b, c.a);
	GX_Position3f32(a.x, y1, a.z); GX_Color4u8(c.r, c.g, c.b, c.a);
	GX_End();
}

static void glow_sprite(v3 c, float size, GXColor gc)
{
	v3 r = v3_scale(G.right, size), u = v3_scale(G.up, size);
	gfx_depth(true, false);
	gfx_blend_add();
	mat_vcolor_tex(&T_GLOW, true, false);
	vdesc_pct();
	GX_Begin(GX_QUADS, VF_3D, 4);
	GX_Position3f32(c.x - r.x + u.x, c.y - r.y + u.y, c.z - r.z + u.z);
	GX_Color4u8(gc.r, gc.g, gc.b, gc.a); GX_TexCoord2f32(0, 0);
	GX_Position3f32(c.x + r.x + u.x, c.y + r.y + u.y, c.z + r.z + u.z);
	GX_Color4u8(gc.r, gc.g, gc.b, gc.a); GX_TexCoord2f32(1, 0);
	GX_Position3f32(c.x + r.x - u.x, c.y + r.y - u.y, c.z + r.z - u.z);
	GX_Color4u8(gc.r, gc.g, gc.b, gc.a); GX_TexCoord2f32(1, 1);
	GX_Position3f32(c.x - r.x - u.x, c.y - r.y - u.y, c.z - r.z - u.z);
	GX_Color4u8(gc.r, gc.g, gc.b, gc.a); GX_TexCoord2f32(0, 1);
	GX_End();
	gfx_blend_none();
	gfx_depth(true, true);
}

static void draw_glass(const daylight *d, float lamp)
{
	rgbf day = rgb_scale(rgb_lerp(d->sky_horizon, d->sky_top, 0.4f), 0.88f);
	rgbf lit = rgb_scale(rgb_from(ALB_WINDOW_LIGHT), 1.1f);
	float glow = saturatef(d->window_glow * 1.15f) * lamp;
	GXColor cab = gx_col(rgb_lerp(rgb_scale(day, 1.0f - 0.55f * d->night), lit, glow), 1.0f);
	GXColor cabin = gx_col(rgb_scale(day, 0.8f - 0.45f * d->night), 1.0f);

	gfx_model_identity();
	mat_vcolor();
	GX_SetCullMode(GX_CULL_FRONT);
	vdesc_pc();
	if (visible(PG_TOWER)) {
		const float X = TOWER_X, Z = TOWER_Z, c = TOWER_CAB_HALF - 0.03f, dh = TOWER_DOOR_HALF;
		const float y0 = world_pad_height() + TOWER_DECK_Y + 1.0f, y1 = y0 + 1.38f, door = y0 + 1.05f;
		glass_quad(v3_make(X + c, 0, Z - c), v3_make(X - c, 0, Z - c), y0, y1, cab);     /* north */
		glass_quad(v3_make(X + c, 0, Z + c), v3_make(X + c, 0, Z - c), y0, y1, cab);     /* east */
		glass_quad(v3_make(X - c, 0, Z - c), v3_make(X - c, 0, Z + c), y0, y1, cab);     /* west */
		glass_quad(v3_make(X - c, 0, Z + c), v3_make(X - dh, 0, Z + c), y0, y1, cab);    /* south, */
		glass_quad(v3_make(X + dh, 0, Z + c), v3_make(X + c, 0, Z + c), y0, y1, cab);    /* either side */
		glass_quad(v3_make(X - dh, 0, Z + c), v3_make(X + dh, 0, Z + c), door, y1, cab); /* of the door */
	}
	if (visible(PG_CABIN)) {
		const float x = CABIN_X - CABIN_HALF_X - 0.02f, f = world_cabin_floor();
		glass_quad(v3_make(x, 0, CABIN_Z - 0.5f), v3_make(x, 0, CABIN_Z + 0.5f), f + 0.9f, f + 1.6f, cabin);
	}

	/* The lit cab glows across the valley; the lamp itself glows inside. */
	if (glow > 0.03f && visible(PG_TOWER)) {
		float dy = world_pad_height() + TOWER_DECK_Y;
		glow_sprite(v3_make(TOWER_X, dy + 1.65f, TOWER_Z), 4.5f,
		            gx_col(rgb_from(ALB_WINDOW_LIGHT), 0.5f * glow));
		glow_sprite(v3_make(TOWER_X + 1.7f, dy + 1.0f, TOWER_Z + 0.82f), 0.5f,
		            gx_col(rgb_from(ALB_WINDOW_LIGHT), 0.7f * lamp));
	}
}

void props_draw(const daylight *d, const props_state *st)
{
	gfx_model_identity();
	gfx_depth(true, true);
	gfx_blend_none();
	mat_lit_solid();
	mat_ramp_light(d->key_dir);
	vdesc_pnc_flush();
	for (int g = 0; g < PG_COUNT; g++) {
		if (g == PG_TOWER_IN || g == PG_CABIN_IN || g == PG_BINOCULARS) continue;
		if ((g == PG_SHUTTERS_OPEN && st->shutters_shut) || (g == PG_SHUTTERS_SHUT && !st->shutters_shut)) continue;
		if (g == PG_CAIRN_STONE && !st->cairn_stone) continue;
		if (visible(g)) dl_draw(&s_groups[g].dl);
	}

	/* Interiors: dimmer than outdoors, plus the lamp. */
	rgbf dim = rgb_make(0.72f, 0.70f, 0.70f);
	rgbf lamp = rgb_scale(rgb_from(ALB_WINDOW_LIGHT), 0.42f * st->lamp);
	if (visible(PG_TOWER_IN) || (!st->binoculars_taken && visible(PG_BINOCULARS))) {
		mat_lit_interior(dim, lamp);
		mat_ramp_light(d->key_dir);
		vdesc_pnc_flush();
		dl_draw(&s_groups[PG_TOWER_IN].dl);
		if (!st->binoculars_taken) dl_draw(&s_groups[PG_BINOCULARS].dl);
	}
	if (visible(PG_CABIN_IN)) {
		mat_lit_interior(rgb_scale(dim, 0.9f), rgb_make(0, 0, 0));
		mat_ramp_light(d->key_dir);
		vdesc_pnc_flush();
		dl_draw(&s_groups[PG_CABIN_IN].dl);
	}
	draw_glass(d, st->lamp);
}
