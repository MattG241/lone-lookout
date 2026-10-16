/*
 * weather_fx.c -- rain: a few hundred short streaks in a cylinder around the
 * eye, falling and slanting with the wind. Each streak's position is a hash
 * of its index, so there is no particle state to update.
 */
#include <math.h>
#include "gfx/weather_fx.h"
#include "gfx/gfx.h"
#include "core/noise.h"
#include "world/world.h"

#define STREAKS 420
#define RADIUS 14.0f
#define HEIGHT 14.0f

void rain_draw(const daylight *d, float rain, float wind, bool indoor_eye)
{
	if (rain < 0.02f) return;
	int n = (int)(STREAKS * rain);
	float t = G.frame / 60.0f;
	rgbf col = rgb_lerp(d->fog, rgb_make(0.85f, 0.88f, 0.92f), 0.4f);
	GXColor c = gx_col(col, 0.55f * rain);
	float slant = 0.12f + 0.25f * wind;
	gfx_model_identity();
	gfx_depth(true, false);
	gfx_blend_alpha();
	mat_vcolor();
	vdesc_pc();
	GX_SetLineWidth(12, GX_TO_ZERO);
	gx_begin(GX_LINES, VF_3D, n * 2);
	for (int i = 0; i < n; i++) {
		u32 h = hash32((u32)i * 2654435761u + 17u);
		float a = (h & 1023) / 1023.0f * LL_TAU, r = sqrtf(((h >> 10) & 1023) / 1023.0f) * RADIUS;
		float speed = 7.0f + ((h >> 20) & 15) * 0.25f;
		float ox = cosf(a) * r, oz = sinf(a) * r;
		if (indoor_eye && fabsf(ox) < 2.6f && fabsf(oz) < 2.6f) ox += 3.0f * signf(ox);
		float y = HEIGHT - fmodf(t * speed + (h >> 24) * 0.1f, HEIGHT) - HEIGHT * 0.4f;
		float x = G.eye.x + ox, z = G.eye.z + oz, yy = G.eye.y + y;
		GX_Position3f32(x, yy, z);
		GX_Color4u8(c.r, c.g, c.b, 0);
		GX_Position3f32(x - slant * 0.6f, yy - 0.6f, z - slant * 0.3f);
		GX_Color4u8(c.r, c.g, c.b, c.a);
	}
	GX_End();
	gfx_blend_none();
	gfx_depth(true, true);
}
