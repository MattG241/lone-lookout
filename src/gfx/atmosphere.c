/* Small, world-anchored details. At most 112 triangles; no particle allocations. */
#include <math.h>
#include "gfx/atmosphere.h"
#include "gfx/gfx.h"
#include "core/noise.h"
#include "game/game.h"

static float s_time;
void atmosphere_update(float dt) { s_time += dt; }

static void sprite(v3 p, float size, rgbf col, float alpha)
{
	v3 r = v3_scale(G.right, size), u = v3_scale(G.up, size);
	GXColor c = gx_col(col, alpha);
	gx_begin(GX_QUADS, VF_3D, 4);
	for (int k = 0; k < 4; k++) {
		float sx = k == 0 || k == 3 ? -1 : 1, sy = k < 2 ? 1 : -1;
		v3 v = v3_add(p, v3_add(v3_scale(r, sx), v3_scale(u, sy)));
		GX_Position3f32(v.x, v.y, v.z);
		GX_Color4u8(c.r, c.g, c.b, c.a);
		GX_TexCoord2f32(sx < 0 ? 0 : 1, sy > 0 ? 0 : 1);
	}
	GX_End();
}

void atmosphere_draw(const daylight *d, bool indoors)
{
	if (indoors || WX.rain > 0.25f || WX.storm > 0.3f) return;
	const int count = 44;
	const float span = 44.0f;
	bool night = d->night > 0.5f;
	/* Fireflies belong to the damp, low ground, only in late summer. */
	bool fireflies = night && game_season() < 0.75f &&
	                 (world_area_at(G.eye.x, G.eye.z) == AREA_TARN || world_area_at(G.eye.x, G.eye.z) == AREA_MEADOW);
	if (night && !fireflies) return;
	gfx_model_identity();
	gfx_depth(true, false);
	gfx_blend_alpha();
	mat_vcolor_tex(&T_GLOW, true, false);
	vdesc_pct();
	float season = game_season();
	for (int i = 0; i < count; i++) {
		u32 h = hash32((u32)i * 2654435761u + 921u);
		float phase = (h & 1023) / 1023.0f * LL_TAU;
		float x = ((h >> 10) & 1023) / 1023.0f * span;
		float z = ((h >> 20) & 1023) / 1023.0f * span;
		/* Wrapping occurs outside the visible fade radius. */
		x += floorf((G.eye.x - x + span * 0.5f) / span) * span;
		z += floorf((G.eye.z - z + span * 0.5f) / span) * span;
		x += sinf(s_time * 0.25f + phase) * 0.6f;
		z += cosf(s_time * 0.18f + phase) * 0.6f;
		float dx = x - G.eye.x, dz = z - G.eye.z;
		float near = 1.0f - smoothstepf(10.0f, 19.0f, sqrtf(dx * dx + dz * dz));
		if (near < 0.01f) continue;
		float y = world_height(x, z);
		float tx = x - TARN_X, tz = z - TARN_Z;
		if (tx * tx + tz * tz < TARN_RADIUS * TARN_RADIUS) y = fmaxf(y, TARN_WATER_Y);
		float cycle = fmodf(s_time * (0.07f + (h & 7) * 0.004f) + phase, 1.0f);
		float alpha = near * (fireflies ? 0.75f * powf(0.5f + 0.5f * sinf(s_time * 1.5f + phase), 3) : 0.22f);
		if (fireflies) y += 0.7f + 0.5f * sinf(s_time * 0.6f + phase);
		else if (season > 0.45f) y += 0.3f + (1.0f - cycle) * 7.0f;
		else y += 0.6f + cycle * 4.0f;
		/* Keep high catwalks clear of particles that belong to the forest floor. */
		if (fabsf(y - G.eye.y) > 10.0f) continue;
		rgbf c = fireflies ? rgb_make(0.85f, 1.0f, 0.35f)
		          : rgb_lerp(rgb_make(1.0f, 0.93f, 0.70f), rgb_make(0.90f, 0.48f, 0.14f), season);
		sprite(v3_make(x, y, z), fireflies ? 0.11f : 0.055f + 0.04f * season, c, alpha);
	}
	/* Low mist over the tarn at dawn. It thins as the sun climbs. */
	float dawn = (1.0f - smoothstepf(6.0f, 8.5f, d->hour)) * smoothstepf(4.5f, 5.7f, d->hour);
	if (dawn > 0.01f && world_area_at(G.eye.x, G.eye.z) == AREA_TARN) {
		for (int i = 0; i < 12; i++) {
			float p = i * 2.399963f;
			v3 pos = v3_make(TARN_X + cosf(p) * (15 + i * 2), TARN_WATER_Y + 0.6f + sinf(p) * 0.3f,
			                  TARN_Z + sinf(p) * (15 + i * 2));
			sprite(pos, 9.0f, rgb_lerp(d->fog, d->sky_horizon, 0.5f), dawn * 0.055f);
		}
	}
	gfx_blend_none();
	gfx_depth(true, true);
}
