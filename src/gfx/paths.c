/*
 * paths.c -- trails and the creek as soft ribbons laid over the ground.
 *
 * The terrain's 4 m grid is far too coarse to paint a 1.8 m footpath, so each
 * trail is a strip draped on the exact terrain surface a few centimetres up:
 * opaque along its centre, feathering to nothing at its edges, lit by the
 * same light ramp as the ground. The creek is the same idea in water colours.
 */
#include <math.h>
#include "gfx/paths.h"
#include "gfx/gfx.h"
#include "gfx/mesh.h"
#include "world/world.h"
#include "palette_data.h"

#define STEP 1.25f
#define MAX_RIBBONS (WORLD_NUM_TRAILS + 1)

typedef struct {
	dlist_t dl;
	v3 centre;
	float radius;
} ribbon_t;

static ribbon_t s_rib[MAX_RIBBONS];
static int s_n;

typedef struct {
	float half_w, lift;
	rgbf col;
	float alpha;
} ribbon_style;

static int resample(const float (*pts)[2], int n, float out[][2], int max_out)
{
	int k = 0;
	out[k][0] = pts[0][0];
	out[k][1] = pts[0][1];
	k++;
	for (int i = 0; i + 1 < n && k < max_out; i++) {
		float dx = pts[i + 1][0] - pts[i][0], dz = pts[i + 1][1] - pts[i][1];
		float len = sqrtf(dx * dx + dz * dz);
		int steps = (int)ceilf(len / STEP);
		for (int s = 1; s <= steps && k < max_out; s++, k++) {
			out[k][0] = pts[i][0] + dx * s / steps;
			out[k][1] = pts[i][1] + dz * s / steps;
		}
	}
	return k;
}

static void build_ribbon(ribbon_t *r, const float (*pts)[2], int n, const ribbon_style *st)
{
	static float p[1024][2];
	int m = resample(pts, n, p, 1024);
	if (m < 2) return;
	u32 c_mid = rgba_u32(st->col, st->alpha), c_edge = rgba_u32(st->col, 0.0f);
	dl_begin(&r->dl, (m - 1) * (3 + 8 * 28) + 64, vdesc_pnc_flush);
	float minx = 1e9f, maxx = -1e9f, minz = 1e9f, maxz = -1e9f, miny = 1e9f, maxy = -1e9f;
	v3 prevL = { 0 }, prevC = { 0 }, prevR = { 0 };
	v3 prevN = { 0 };
	for (int i = 0; i < m; i++) {
		int a = i > 0 ? i - 1 : i, b = i < m - 1 ? i + 1 : i;
		float tx = p[b][0] - p[a][0], tz = p[b][1] - p[a][1];
		float tl = sqrtf(tx * tx + tz * tz);
		tx /= tl;
		tz /= tl;
		float lx = tz, lz = -tx;                      /* left of the direction of travel */
		float x = p[i][0], z = p[i][1];
		v3 L = v3_make(x + lx * st->half_w, 0, z + lz * st->half_w);
		v3 R = v3_make(x - lx * st->half_w, 0, z - lz * st->half_w);
		v3 C = v3_make(x, world_height(x, z) + st->lift, z);
		L.y = world_height(L.x, L.z) + st->lift * 0.7f;
		R.y = world_height(R.x, R.z) + st->lift * 0.7f;
		v3 N = world_normal(x, z);
		if (i > 0) {
			GX_Begin(GX_QUADS, VF_3D, 8);
			emit_pnc(prevL, prevN, c_edge);
			emit_pnc(prevC, prevN, c_mid);
			emit_pnc(C, N, c_mid);
			emit_pnc(L, N, c_edge);
			emit_pnc(prevC, prevN, c_mid);
			emit_pnc(prevR, prevN, c_edge);
			emit_pnc(R, N, c_edge);
			emit_pnc(C, N, c_mid);
			GX_End();
		}
		prevL = L;
		prevC = C;
		prevR = R;
		prevN = N;
		minx = fminf(minx, x);
		maxx = fmaxf(maxx, x);
		minz = fminf(minz, z);
		maxz = fmaxf(maxz, z);
		miny = fminf(miny, C.y);
		maxy = fmaxf(maxy, C.y);
	}
	dl_end(&r->dl);
	r->centre = v3_make((minx + maxx) * 0.5f, (miny + maxy) * 0.5f, (minz + maxz) * 0.5f);
	float ex = (maxx - minx) * 0.5f + 2.0f, ey = (maxy - miny) * 0.5f + 2.0f, ez = (maxz - minz) * 0.5f + 2.0f;
	r->radius = sqrtf(ex * ex + ey * ey + ez * ez);
}

void paths_build(void)
{
	const ribbon_style trail = { 0.95f, 0.07f, rgb_scale(rgb_from(ALB_TRAIL), 1.06f), 0.88f };
	const ribbon_style creek = { 1.05f, 0.09f, rgb_lerp(rgb_from(ALB_WATER), rgb_make(0.30f, 0.38f, 0.42f), 0.35f), 0.92f };
	s_n = 0;
	for (int t = 0; t < world_trail_count() && s_n < MAX_RIBBONS; t++) {
		int n;
		const float (*pts)[2] = world_trail(t, &n);
		build_ribbon(&s_rib[s_n++], pts, n, &trail);
	}
	int n;
	const float (*cp)[2] = world_creek(&n);
	build_ribbon(&s_rib[s_n++], cp, n, &creek);
}

void paths_draw(const daylight *d)
{
	gfx_model_identity();
	gfx_depth(true, false);
	gfx_blend_alpha();
	mat_lit_solid();
	GX_SetCullMode(GX_CULL_NONE);
	mat_ramp_light(d->key_dir);
	vdesc_pnc_flush();
	for (int i = 0; i < s_n; i++)
		if (s_rib[i].dl.len && gfx_sphere_visible(s_rib[i].centre, s_rib[i].radius))
			dl_draw(&s_rib[i].dl);
	gfx_blend_none();
	gfx_depth(true, true);
}
