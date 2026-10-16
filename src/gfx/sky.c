/*
 * sky.c -- the sky is a vertex-coloured dome recoloured every frame from the
 * colour script; distant ranges are three alpha-cut silhouette rings, tinted
 * toward the haze colour by distance, like layers of a paper diorama.
 */
#include <math.h>
#include "gfx/sky.h"
#include "gfx/gfx.h"
#include "core/noise.h"
#include "assets.h"
#include "palette_data.h"

#define DOME_SEG 24
#define DOME_RINGS 9
#define DOME_R 1200.0f
#define NUM_STARS 260
#define NUM_CLOUDS 22

static v3 s_dome_dir[DOME_RINGS + 1][DOME_SEG + 1];
static v3 s_star_dir[NUM_STARS];
static float s_star_mag[NUM_STARS];
static v3 s_cloud_dir[NUM_CLOUDS];
static float s_cloud_size[NUM_CLOUDS], s_cloud_level[NUM_CLOUDS];

void sky_init(void)
{
	/* Rings from 12 degrees below the horizon up to the zenith. */
	for (int r = 0; r <= DOME_RINGS; r++) {
		float t = (float)r / DOME_RINGS;
		float el = DEG2RAD(-12.0f + 102.0f * powf(t, 1.4f));
		if (el > LL_PI * 0.5f) el = LL_PI * 0.5f;
		for (int s = 0; s <= DOME_SEG; s++) {
			float az = LL_TAU * s / DOME_SEG;
			s_dome_dir[r][s] = v3_make(cosf(az) * cosf(el), sinf(el), sinf(az) * cosf(el));
		}
	}
	rng_t rng;
	rng_seed(&rng, 2024);
	for (int i = 0; i < NUM_STARS; i++) {
		float az = rng_float(&rng) * LL_TAU;
		float el = asinf(rng_range(&rng, 0.08f, 1.0f));
		s_star_dir[i] = v3_make(cosf(az) * cosf(el), sinf(el), sinf(az) * cosf(el));
		float m = rng_float(&rng);
		s_star_mag[i] = m * m * m;
	}
	for (int i = 0; i < NUM_CLOUDS; i++) {
		float az = rng_float(&rng) * LL_TAU;
		float el = DEG2RAD(rng_range(&rng, 6.0f, 32.0f));
		s_cloud_dir[i] = v3_make(cosf(az) * cosf(el), sinf(el), sinf(az) * cosf(el));
		s_cloud_size[i] = rng_range(&rng, 60.0f, 130.0f);
		s_cloud_level[i] = rng_float(&rng);   /* which cloud amounts show this one */
	}
}

/* Soft paper-cut clouds: stretched glow sprites, lit warm on the sun side. */
static void draw_clouds(const daylight *d, float cloud)
{
	float amount = 0.12f + cloud;
	mat_vcolor_tex(&T_GLOW, true, false);
	vdesc_pct();
	gfx_blend_alpha();
	float drift = G.frame * 0.00004f;
	for (int i = 0; i < NUM_CLOUDS; i++) {
		if (s_cloud_level[i] > amount) continue;
		float fade = saturatef((amount - s_cloud_level[i]) * 4.0f);
		float ca = cosf(drift), sa = sinf(drift);
		v3 dir = s_cloud_dir[i];
		dir = v3_make(dir.x * ca - dir.z * sa, dir.y, dir.x * sa + dir.z * ca);
		float lit = saturatef(v3_dot(dir, d->sun_dir) * 0.5f + 0.5f);
		rgbf base = rgb_lerp(d->sky_horizon, rgb_make(0.97f, 0.96f, 0.95f), 0.45f * (1.0f - d->night));
		base = rgb_lerp(base, rgb_scale(d->fog, 0.85f), cloud * 0.5f);
		rgbf col = rgb_add(rgb_scale(base, 0.85f + 0.15f * lit), rgb_scale(d->sun, 0.25f * lit * d->sun_vis));
		col = rgb_scale(col, 1.0f - 0.65f * d->night);
		GXColor gc = gx_col(col, (0.35f + 0.4f * cloud) * fade);
		v3 c = v3_scale(dir, 950.0f);
		/* Horizontal stretch: along the sky's tangent, not the camera's tilt. */
		v3 t = v3_norm(v3_make(-dir.z, 0.0f, dir.x));
		v3 u = v3_norm(v3_cross(dir, t));
		for (int k = -1; k <= 1; k++) {
			float sz = s_cloud_size[i] * (k ? 0.7f : 1.0f);
			v3 cc = v3_add(c, v3_add(v3_scale(t, k * sz * 1.1f), v3_scale(u, (k ? -0.15f : 0.1f) * sz)));
			v3 r = v3_scale(t, sz * 1.8f), up = v3_scale(u, sz * 0.55f);
			gx_begin(GX_QUADS, VF_3D, 4);
			GX_Position3f32(cc.x - r.x + up.x, cc.y - r.y + up.y, cc.z - r.z + up.z);
			GX_Color4u8(gc.r, gc.g, gc.b, gc.a); GX_TexCoord2f32(0, 0);
			GX_Position3f32(cc.x + r.x + up.x, cc.y + r.y + up.y, cc.z + r.z + up.z);
			GX_Color4u8(gc.r, gc.g, gc.b, gc.a); GX_TexCoord2f32(1, 0);
			GX_Position3f32(cc.x + r.x - up.x, cc.y + r.y - up.y, cc.z + r.z - up.z);
			GX_Color4u8(gc.r, gc.g, gc.b, gc.a); GX_TexCoord2f32(1, 1);
			GX_Position3f32(cc.x - r.x - up.x, cc.y - r.y - up.y, cc.z - r.z - up.z);
			GX_Color4u8(gc.r, gc.g, gc.b, gc.a); GX_TexCoord2f32(0, 1);
			GX_End();
		}
	}
	gfx_blend_none();
}

static rgbf sky_color(const daylight *d, v3 dir)
{
	float el = dir.y;
	if (el < 0.0f) {
		/* Below the horizon: fade to the fog colour so terrain haze matches. */
		return rgb_lerp(d->sky_horizon, d->fog, smoothstepf(0.0f, -0.08f, el));
	}
	float t = powf(saturatef(el), 0.55f);
	rgbf c = rgb_lerp(d->sky_horizon, d->sky_top, t);
	/* Warm glow around the sun, strongest when it is low. */
	float cosang = v3_dot(dir, d->sun_dir);
	float low = 1.0f - saturatef(d->sun_dir.y * 2.2f);
	float glow = expf((cosang - 1.0f) * 4.5f) * (0.25f + 0.55f * low) * smoothstepf(-0.25f, 0.02f, d->sun_dir.y);
	c = rgb_add(c, rgb_scale(d->sun, glow * 0.6f));
	/* Horizon band brightens a touch toward the sun's side. */
	float band = (1.0f - saturatef(el * 6.0f)) * saturatef(cosang * 0.5f + 0.5f) * 0.12f * (1.0f - d->night);
	c = rgb_add(c, rgb_scale(d->sun, band));
	return c;
}

static void draw_glow(v3 dir, float dist, float size, rgbf col, float alpha)
{
	v3 c = v3_scale(dir, dist);
	v3 r = v3_scale(G.right, size);
	v3 u = v3_scale(G.up, size);
	GXColor gc = gx_col(col, alpha);
	gx_begin(GX_QUADS, VF_3D, 4);
	GX_Position3f32(c.x - r.x + u.x, c.y - r.y + u.y, c.z - r.z + u.z);
	GX_Color4u8(gc.r, gc.g, gc.b, gc.a); GX_TexCoord2f32(0, 0);
	GX_Position3f32(c.x + r.x + u.x, c.y + r.y + u.y, c.z + r.z + u.z);
	GX_Color4u8(gc.r, gc.g, gc.b, gc.a); GX_TexCoord2f32(1, 0);
	GX_Position3f32(c.x + r.x - u.x, c.y + r.y - u.y, c.z + r.z - u.z);
	GX_Color4u8(gc.r, gc.g, gc.b, gc.a); GX_TexCoord2f32(1, 1);
	GX_Position3f32(c.x - r.x - u.x, c.y - r.y - u.y, c.z - r.z - u.z);
	GX_Color4u8(gc.r, gc.g, gc.b, gc.a); GX_TexCoord2f32(0, 1);
	GX_End();
}

void sky_draw(const daylight *d, float cloud)
{
	gfx_model_follow_eye();
	gfx_depth(false, false);
	gfx_fog_off();
	gfx_blend_none();
	mat_vcolor();
	vdesc_pc();

	static rgbf cols[DOME_RINGS + 1][DOME_SEG + 1];
	for (int r = 0; r <= DOME_RINGS; r++)
		for (int s = 0; s <= DOME_SEG; s++)
			cols[r][s] = sky_color(d, s_dome_dir[r][s]);

	for (int r = 0; r < DOME_RINGS; r++) {
		gx_begin(GX_TRIANGLESTRIP, VF_3D, (DOME_SEG + 1) * 2);
		for (int s = 0; s <= DOME_SEG; s++) {
			for (int k = 1; k >= 0; k--) {
				v3 p = v3_scale(s_dome_dir[r + k][s], DOME_R);
				GXColor c = gx_col(cols[r + k][s], 1.0f);
				GX_Position3f32(p.x, p.y, p.z);
				GX_Color4u8(c.r, c.g, c.b, 255);
			}
		}
		GX_End();
	}

	/* Stars: single-pixel points that fade in after dusk. */
	if (d->night > 0.01f) {
		gfx_blend_add();
		GX_SetPointSize(9, GX_TO_ZERO);   /* 1.5 px */
		gx_begin(GX_POINTS, VF_3D, NUM_STARS);
		for (int i = 0; i < NUM_STARS; i++) {
			v3 p = v3_scale(s_star_dir[i], DOME_R * 0.98f);
			float tw = 0.75f + 0.25f * sinf(G.frame * 0.05f + i * 1.7f);
			u8 a = (u8)(saturatef(d->night * (0.25f + 0.75f * s_star_mag[i]) * tw) * 255.0f);
			GX_Position3f32(p.x, p.y, p.z);
			GX_Color4u8(235, 238, 255, a);
		}
		GX_End();
	}

	/* Sun and moon: one soft glow sprite each, additive. */
	mat_vcolor_tex(&T_GLOW, true, false);
	vdesc_pct();
	gfx_blend_add();
	if (d->sun_vis > 0.0f) {
		draw_glow(d->sun_dir, 900.0f, 70.0f, rgb_lerp(d->sun, rgb_make(1, 1, 1), 0.35f), d->sun_vis);
		draw_glow(d->sun_dir, 900.0f, 190.0f, d->sun, d->sun_vis * 0.35f);
	}
	if (d->night > 0.0f)
		draw_glow(d->moon_dir, 900.0f, 26.0f, rgb_make(0.86f, 0.89f, 0.95f), d->night * (1.0f - cloud * 0.8f));
	gfx_blend_none();
	draw_clouds(d, cloud);
}

/* ------------------------------------------------------------------------ */
/* Backdrop rings                                                           */
/* ------------------------------------------------------------------------ */

typedef struct {
	float radius;
	float top_deg;       /* angular height of the texture's top row */
	float bottom_y;      /* metres below eye (hidden behind terrain) */
	float u_repeat;
	int layer;           /* texture layer (0 = far peaks) */
	float haze;          /* how much of the fog colour to mix in */
	const float *albedo;
	float spin;          /* rotate so repeats do not line up */
} ring_def;

static const ring_def RINGS[3] = {
	{ 1150.0f, 12.0f, -60.0f, 2.0f, 0, 0.62f, ALB_ASH_SCREE, 0.00f },
	{ 820.0f, 7.0f, -60.0f, 3.0f, 1, 0.45f, ALB_PINE, 0.37f },
	{ 560.0f, 4.5f, -60.0f, 3.0f, 2, 0.30f, ALB_PINE_DARK, 0.71f },
};

static void draw_rings(const daylight *d, int first, int last)
{
	gfx_model_follow_eye();
	gfx_depth(false, false);
	gfx_fog_off();
	gfx_blend_alpha();
	mat_vcolor_tex(&T_BACKDROP, false, true);
	vdesc_pct();
	const int SEG = 48;
	const float layer_v = 1.0f / 3.0f;

	for (int ri = first; ri <= last; ri++) {
		const ring_def *R = &RINGS[ri];
		rgbf alb = rgb_from(R->albedo);
		/* Rough lighting: distant ranges catch the key light dimly. */
		rgbf lit = rgb_mul(alb, rgb_add(d->ambient, rgb_scale(d->sun, d->key_strength * 0.45f)));
		float haze = saturatef(R->haze * (0.6f + 0.6f * d->fog_density));
		/* Never clearer than the far edge of the real terrain in front of it, or in thick
		 * haze the fogged-out rim shows as a pale cut-out against darker ranges behind. */
		float end = 470.0f / clampf(d->fog_density, 0.25f, 1.6f), k = (340.0f + 0.1f * R->radius) / end;
		haze = fmaxf(haze, 1.0f - exp2f(-8.0f * k * k));
		rgbf top = rgb_lerp(lit, d->fog, haze);
		rgbf bot = rgb_lerp(lit, d->fog, saturatef(haze + 0.35f));
		/* Texture intensity is centred on 0.5 and doubled in the TEV. */
		GXColor ct = gx_col(top, 1.0f), cb = gx_col(bot, 1.0f);
		float y_top = R->radius * tanf(DEG2RAD(R->top_deg));
		const float texel = 1.0f / (3.0f * BACKDROP_LAYER_H);
		float v0 = R->layer * layer_v + 1.5f * texel, v1 = (R->layer + 1) * layer_v - 1.5f * texel;
		/* In thick air the range is only a silhouette: a second, fog-coloured pass
		 * flattens the texture's light and dark (the doubled texture would otherwise
		 * still streak paler than the fog). */
		float flat = saturatef((haze - 0.6f) / 0.35f);
		GXColor cf = gx_col(d->fog, flat);
		for (int pass = 0; pass < (flat > 0.01f ? 2 : 1); pass++) {
			if (pass == 1) mat_vcolor_tex(&T_BACKDROP, true, false);
			gx_begin(GX_TRIANGLESTRIP, VF_3D, (SEG + 1) * 2);
			for (int s = 0; s <= SEG; s++) {
				float a = LL_TAU * s / SEG + R->spin;
				float x = cosf(a) * R->radius, z = sinf(a) * R->radius;
				float u = R->u_repeat * s / SEG;
				GXColor c0 = pass ? cf : ct, c1 = pass ? cf : cb;
				GX_Position3f32(x, y_top, z);
				GX_Color4u8(c0.r, c0.g, c0.b, pass ? c0.a : 255);
				GX_TexCoord2f32(u, v0);
				GX_Position3f32(x, R->bottom_y, z);
				GX_Color4u8(c1.r, c1.g, c1.b, pass ? c1.a : 255);
				GX_TexCoord2f32(u, v1);
			}
			GX_End();
			if (pass == 1) mat_vcolor_tex(&T_BACKDROP, false, true);
		}
	}
	gfx_blend_none();
}

void backdrop_draw(const daylight *d) { draw_rings(d, 0, 2); }
void backdrop_draw_far(const daylight *d) { draw_rings(d, 0, 0); }
void backdrop_draw_near(const daylight *d) { draw_rings(d, 1, 2); }
