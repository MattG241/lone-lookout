/*
 * props_tower.c -- Kestrel Lookout, built from beams and boxes in code.
 *
 * A 9 m timber tower: four splayed legs with girts and X-bracing, a
 * catwalk with railing, and a 4 x 4 m cab with a pyramid roof. Four flights
 * of stairs wind round the outside (south, east, north, west) with landings
 * at the corners, matching the walk patches in world/walk.c. Inside: the
 * fire finder, cot, stove and kettle, the radio table with logbook and lamp,
 * Augie's shelf, and the lightning stool on its glass feet.
 */
#include <math.h>
#include "gfx/props_int.h"

static float s_pad;

/* Tower-local to world: u east, v south, y above the pad. */
static v3 T(float u, float y, float v) { return v3_make(TOWER_X + u, s_pad + y, TOWER_Z + v); }

static u32 wood(float k) { return colp(ALB_TOWER_WOOD, k); }

static v3 leg_point(int corner, float y_frac)
{
	float sx = (corner & 1) ? 1.0f : -1.0f, sz = (corner & 2) ? 1.0f : -1.0f;
	float spread = lerpf(TOWER_LEG_BASE, TOWER_LEG_TOP, y_frac);
	return T(sx * spread, y_frac * TOWER_DECK_Y, sz * spread);
}

static void frame(void)
{
	static const int ring[4] = { 0, 1, 3, 2 };   /* corners in perimeter order */
	u32 leg = wood(0.78f), brace = wood(0.9f);
	for (int c = 0; c < 4; c++)
		mesh_beam(v3_sub(leg_point(c, 0.0f), v3_make(0, 0.5f, 0)), leg_point(c, 1.0f), 0.26f, 0.26f, leg);
	const float levels[4] = { 0.02f, 1.0f / 3.0f, 2.0f / 3.0f, 0.98f };
	for (int f = 0; f < 4; f++) {
		int a = ring[f], b = ring[(f + 1) & 3];
		for (int l = 1; l < 4; l++)
			mesh_beam(leg_point(a, levels[l]), leg_point(b, levels[l]), 0.12f, 0.16f, brace);
		for (int l = 0; l < 3; l++) {
			mesh_beam(leg_point(a, levels[l]), leg_point(b, levels[l + 1]), 0.08f, 0.1f, brace);
			mesh_beam(leg_point(b, levels[l]), leg_point(a, levels[l + 1]), 0.08f, 0.1f, brace);
		}
	}
}

static void catwalk(void)
{
	const float D = TOWER_DECK_Y, e = TOWER_DECK_HALF - 0.05f;
	u32 deck = wood(1.1f), rail = wood(1.0f);
	mesh_box(T(0, D - 0.09f, 0), v3_make(TOWER_DECK_HALF, 0.09f, TOWER_DECK_HALF), deck);
	/* Joists showing underneath. */
	for (int k = -2; k <= 2; k++)
		mesh_box(T(k * 1.4f, D - 0.28f, 0), v3_make(0.06f, 0.1f, TOWER_DECK_HALF - 0.05f), wood(0.8f));

	/* Posts every 1.5 m; the west side opens at the top landing (v 2..3). */
	for (int i = 0; i <= 4; i++) {
		float t = -e + 2.0f * e * i / 4.0f;
		v3 pts[4] = { { t, 0, -e }, { t, 0, e }, { -e, 0, t }, { e, 0, t } };
		for (int k = 0; k < 4; k++) {
			if (k == 2 && t > STAIR_TOP_V - 0.1f) continue;
			mesh_beam(T(pts[k].x, D, pts[k].z), T(pts[k].x, D + 1.02f, pts[k].z), 0.08f, 0.08f, rail);
		}
	}
	mesh_beam(T(-e, D, STAIR_TOP_V), T(-e, D + 1.02f, STAIR_TOP_V), 0.08f, 0.08f, rail);
	for (int h = 0; h < 2; h++) {
		float y = D + (h ? 1.0f : 0.5f), w = h ? 0.1f : 0.06f;
		mesh_beam(T(-e, y, -e), T(e, y, -e), 0.07f, w, rail);
		mesh_beam(T(e, y, -e), T(e, y, e), 0.07f, w, rail);
		mesh_beam(T(-e, y, e), T(e, y, e), 0.07f, w, rail);
		mesh_beam(T(-e, y, -e), T(-e, y, STAIR_TOP_V), 0.07f, w, rail);
	}
}

static void cab(void)
{
	const float D = TOWER_DECK_Y, c = TOWER_CAB_HALF, dh = TOWER_DOOR_HALF, t = 0.03f;
	u32 wall = wood(0.95f), post = wood(0.88f), trim = wood(1.05f);
	/* Lower walls as thin panels (so they read from inside and out). */
	mesh_box(T(0, D + 0.5f, -c), v3_make(c, 0.5f, t), wall);
	mesh_box(T(c, D + 0.5f, 0), v3_make(t, 0.5f, c), wall);
	mesh_box(T(-c, D + 0.5f, 0), v3_make(t, 0.5f, c), wall);
	mesh_box(T(-(c + dh) * 0.5f, D + 0.5f, c), v3_make((c - dh) * 0.5f, 0.5f, t), wall);
	mesh_box(T((c + dh) * 0.5f, D + 0.5f, c), v3_make((c - dh) * 0.5f, 0.5f, t), wall);
	/* Sill and corner posts, mullions, door frame. */
	for (int s = 0; s < 4; s++) {
		v3 a = s == 0 ? T(-c, D + 1.0f, -c) : s == 1 ? T(c, D + 1.0f, -c) : s == 2 ? T(c, D + 1.0f, c) : T(-c, D + 1.0f, c);
		v3 b = s == 0 ? T(c, D + 1.0f, -c) : s == 1 ? T(c, D + 1.0f, c) : s == 2 ? T(-c, D + 1.0f, c) : T(-c, D + 1.0f, -c);
		if (s == 2) {
			mesh_beam(T(c, D + 1.0f, c), T(dh, D + 1.0f, c), 0.1f, 0.06f, trim);
			mesh_beam(T(-dh, D + 1.0f, c), T(-c, D + 1.0f, c), 0.1f, 0.06f, trim);
		} else {
			mesh_beam(a, b, 0.1f, 0.06f, trim);
		}
	}
	for (int k = 0; k < 4; k++) {
		float sx = (k & 1) ? 1.0f : -1.0f, sz = (k & 2) ? 1.0f : -1.0f;
		mesh_beam(T(sx * c, D, sz * c), T(sx * c, D + 2.45f, sz * c), 0.14f, 0.14f, post);
	}
	for (int m = -1; m <= 1; m += 2) {
		float o = m * c / 3.0f;
		mesh_beam(T(o, D + 1.0f, -c), T(o, D + 2.4f, -c), 0.06f, 0.06f, post);
		mesh_beam(T(c, D + 1.0f, o), T(c, D + 2.4f, o), 0.06f, 0.06f, post);
		mesh_beam(T(-c, D + 1.0f, o), T(-c, D + 2.4f, o), 0.06f, 0.06f, post);
		mesh_beam(T(m * 1.3f, D + 1.0f, c), T(m * 1.3f, D + 2.4f, c), 0.06f, 0.06f, post);
		mesh_beam(T(m * dh, D, c), T(m * dh, D + 2.4f, c), 0.09f, 0.09f, post);
	}
	mesh_beam(T(-dh, D + 2.05f, c), T(dh, D + 2.05f, c), 0.09f, 0.09f, post);
	/* Header and ceiling. */
	mesh_box(T(0, D + 2.42f, -c), v3_make(c + 0.04f, 0.06f, 0.06f), trim);
	mesh_box(T(0, D + 2.42f, c), v3_make(c + 0.04f, 0.06f, 0.06f), trim);
	mesh_box(T(c, D + 2.42f, 0), v3_make(0.06f, 0.06f, c), trim);
	mesh_box(T(-c, D + 2.42f, 0), v3_make(0.06f, 0.06f, c), trim);
	mesh_box(T(0, D + 2.49f, 0), v3_make(c, 0.03f, c), wood(0.7f));
	/* Roof: a shallow pyramid with a wide overhang, and its soffit. */
	u32 roof = col3(0.47f, 0.44f, 0.42f), soffit = wood(0.62f);
	float rh = D + 2.5f, o = c + 0.6f;
	v3 apex = T(0, D + 3.6f, 0);
	v3 rc[4] = { T(-o, rh, -o), T(o, rh, -o), T(o, rh, o), T(-o, rh, o) };
	for (int i = 0; i < 4; i++) {
		mesh_tri(rc[(i + 1) & 3], rc[i], apex, roof);
		mesh_tri(rc[i], rc[(i + 1) & 3], T(0, rh - 0.02f, 0), soffit);
	}
	/* Lightning rod and the radio aerial. */
	mesh_beam(apex, v3_add(apex, v3_make(0, 1.0f, 0)), 0.03f, 0.03f, C_IRON);
	mesh_beam(T(c - 0.2f, rh + 0.3f, -c + 0.2f), T(c - 0.2f, rh + 2.6f, -c + 0.2f), 0.025f, 0.025f, C_IRON);
	/* The instrument shelter on a bracket off the east railing (morning and
	 * evening readings), and the anemometer cups up on the roof. */
	u32 white = col3(0.90f, 0.89f, 0.85f), slat = col3(0.72f, 0.72f, 0.70f);
	float sx = TOWER_DECK_HALF + 0.3f, sz = -1.6f;
	mesh_box(T(sx, D + 1.25f, sz), v3_make(0.22f, 0.22f, 0.26f), white);
	for (int k = 0; k < 4; k++)
		mesh_box(T(sx + 0.225f, D + 1.1f + k * 0.08f, sz), v3_make(0.006f, 0.012f, 0.24f), slat);
	mesh_box(T(sx, D + 1.5f, sz), v3_make(0.26f, 0.03f, 0.3f), white);
	mesh_beam(T(TOWER_DECK_HALF - 0.05f, D + 1.0f, sz), T(sx, D + 1.02f, sz), 0.05f, 0.05f, C_IRON);
	mesh_beam(T(sx, D + 0.6f, sz), T(sx, D + 1.03f, sz), 0.05f, 0.05f, C_IRON);
	v3 mast = T(c - 0.4f, D + 2.5f, -c + 0.4f);
	mesh_beam(mast, v3_add(mast, v3_make(0, 1.4f, 0)), 0.03f, 0.03f, C_IRON);
	for (int k = 0; k < 3; k++) {
		float a = k * LL_TAU / 3.0f;
		v3 hub = v3_add(mast, v3_make(0, 1.4f, 0)), tip = v3_add(hub, v3_make(cosf(a) * 0.2f, 0, sinf(a) * 0.2f));
		mesh_beam(hub, tip, 0.015f, 0.015f, C_IRON);
		mesh_prism(tip, v3_add(tip, v3_make(0, 0.06f, 0)), 0.04f, 6, C_IRON, C_IRON);
	}
	/* Augie's welcome note beside the door. */
	mesh_box(T(-1.0f, D + 0.72f, c + 0.04f), v3_make(0.11f, 0.14f, 0.006f), C_PAPER);
}

/* One flight of stairs, from foot (u0, v0) to head (u1, v1), centre line of the band. */
static void flight(float u0, float v0, float u1, float v1, float y0, float y1)
{
	u32 stringer = wood(0.8f), tread = wood(1.05f), rail = wood(0.95f);
	float du = u1 - u0, dv = v1 - v0, run = sqrtf(du * du + dv * dv);
	float tu = du / run, tv = dv / run;               /* along */
	float nu = -tv, nv = tu;                          /* across */
	float half = (STAIR_OUT - STAIR_IN) * 0.5f;
	for (int i = 0; i < STAIR_STEPS; i++) {
		float f = (i + 0.5f) / STAIR_STEPS, y = y0 + (y1 - y0) * (i + 1) / STAIR_STEPS;
		float hu = fabsf(tu) * (run / STAIR_STEPS * 0.5f + 0.02f) + fabsf(nu) * (half - 0.02f);
		float hv = fabsf(tv) * (run / STAIR_STEPS * 0.5f + 0.02f) + fabsf(nv) * (half - 0.02f);
		mesh_box(T(u0 + du * f, y - 0.03f, v0 + dv * f), v3_make(hu, 0.03f, hv), tread);
	}
	for (int s = -1; s <= 1; s += 2) {
		float ou = nu * s * (half - 0.03f), ov = nv * s * (half - 0.03f);
		mesh_beam(T(u0 + ou, y0 - 0.2f, v0 + ov), T(u1 + ou, y1 - 0.2f, v1 + ov), 0.07f, 0.28f, stringer);
		float ru = nu * s * (half + 0.02f), rv = nv * s * (half + 0.02f);
		mesh_beam(T(u0 + ru, y0 + 0.95f, v0 + rv), T(u1 + ru, y1 + 0.95f, v1 + rv), 0.06f, 0.07f, rail);
		for (int p = 0; p <= 2; p++) {
			float f = p / 2.0f;
			v3 b = T(u0 + du * f + ru, y0 + (y1 - y0) * f, v0 + dv * f + rv);
			mesh_beam(b, v3_add(b, v3_make(0, 0.97f, 0)), 0.08f, 0.08f, rail);
		}
	}
	/* A support post from the ground under the middle of the outer stringer
	 * (the outer side is the one facing away from the tower). */
	float mu = (u0 + u1) * 0.5f, mv = (v0 + v1) * 0.5f, ou = 0.0f, ov = 0.0f;
	if (fabsf(mu) > fabsf(mv)) ou = signf(mu) * (half - 0.03f);
	else ov = signf(mv) * (half - 0.03f);
	mesh_beam(T(mu + ou, -0.4f, mv + ov), T(mu + ou, (y0 + y1) * 0.5f - 0.3f, mv + ov), 0.14f, 0.14f, wood(0.75f));
}

/* A square landing at a corner of the stair band, posts and rails on its outer sides. */
static void landing(float su, float sv, float y)
{
	float m = (STAIR_IN + STAIR_OUT) * 0.5f, h = (STAIR_OUT - STAIR_IN) * 0.5f, o = STAIR_OUT - 0.04f;
	u32 deck = wood(1.05f), rail = wood(0.95f);
	mesh_box(T(su * m, y - 0.05f, sv * m), v3_make(h, 0.05f, h), deck);
	mesh_beam(T(su * o, -0.4f, sv * o), T(su * o, y + 1.0f, sv * o), 0.14f, 0.14f, wood(0.78f));
	mesh_beam(T(su * o, y + 0.95f, sv * STAIR_IN), T(su * o, y + 0.95f, sv * o), 0.06f, 0.07f, rail);
	mesh_beam(T(su * STAIR_IN, y + 0.95f, sv * o), T(su * o, y + 0.95f, sv * o), 0.06f, 0.07f, rail);
}

static void stairs(void)
{
	const float m = (STAIR_IN + STAIR_OUT) * 0.5f, R = STAIR_RISE;
	flight(-STAIR_IN, m, STAIR_IN, m, 0, R);                       /* A: south, climbing east */
	landing(1, 1, R);
	flight(m, STAIR_IN, m, -STAIR_IN, R, 2 * R);                   /* B: east, climbing north */
	landing(1, -1, 2 * R);
	flight(STAIR_IN, -m, -STAIR_IN, -m, 2 * R, 3 * R);             /* C: north, climbing west */
	landing(-1, -1, 3 * R);
	flight(-m, -STAIR_IN, -m, STAIR_TOP_V, 3 * R, 4 * R);          /* D: west, climbing south */
	/* Top landing, west of the deck's SW corner. */
	u32 deck = wood(1.05f), rail = wood(0.95f);
	float o = STAIR_OUT - 0.04f, D = TOWER_DECK_Y;
	mesh_box(T(-m, D - 0.05f, (STAIR_TOP_V + STAIR_OUT) * 0.5f), v3_make(0.5f, 0.05f, (STAIR_OUT - STAIR_TOP_V) * 0.5f), deck);
	mesh_beam(T(-o, -0.4f, o), T(-o, D + 1.0f, o), 0.14f, 0.14f, wood(0.78f));
	mesh_beam(T(-o, D + 0.95f, STAIR_TOP_V), T(-o, D + 0.95f, o), 0.06f, 0.07f, rail);
	mesh_beam(T(-o, D + 0.95f, o), T(-TOWER_DECK_HALF, D + 0.95f, o), 0.06f, 0.07f, rail);
	mesh_beam(T(-TOWER_DECK_HALF + 0.05f, D, o), T(-TOWER_DECK_HALF + 0.05f, D + 1.0f, o), 0.08f, 0.08f, rail);
	/* Augie's note on the post at the foot of the stairs. */
	mesh_beam(T(-STAIR_IN, -0.3f, STAIR_OUT - 0.04f), T(-STAIR_IN, 1.35f, STAIR_OUT - 0.04f), 0.1f, 0.1f, wood(0.85f));
	mesh_box(T(-STAIR_IN, 1.2f, STAIR_OUT + 0.07f), v3_make(0.09f, 0.12f, 0.006f), C_PAPER);
}

static void base(void)
{
	/* Woodpile against the west side, and the rain cistern to the NE. */
	u32 split = C_CUT, bark = C_BARK;
	for (int r = 0; r < 4; r++)
		for (int k = 0; k < 6; k++) {
			float z = -2.1f + k * 0.44f + (r & 1) * 0.2f;
			v3 a = T(-5.95f, 0.18f + r * 0.3f, z), b = T(-5.25f, 0.18f + r * 0.3f, z);
			if (z > 0.1f) continue;
			mesh_prism(a, b, 0.15f, 6, bark, split);
		}
	mesh_prism(T(5.4f, -0.3f, -4.6f), T(5.4f, 1.6f, -4.6f), 0.75f, 12, col3(0.46f, 0.47f, 0.46f), col3(0.36f, 0.37f, 0.36f));
	mesh_beam(T(5.4f, 1.6f, -4.6f), T(2.0f, 3.2f, -2.0f), 0.08f, 0.08f, C_IRON);   /* downpipe from the deck */
}

static void interior(void)
{
	const float D = TOWER_DECK_Y;
	u32 dark = wood(0.7f), mid = wood(0.95f), light = wood(1.2f);
	/* Fire finder: pedestal, the round table, the sighting ring and two sights. */
	mesh_prism(T(0, D, 0), T(0, D + 0.98f, 0), 0.13f, 8, dark, dark);
	mesh_prism(T(0, D + 0.98f, 0), T(0, D + 1.06f, 0), 0.36f, 14, mid, col3(0.88f, 0.84f, 0.74f));
	mesh_prism(T(0, D + 1.06f, 0), T(0, D + 1.09f, 0), 0.34f, 14, col3(0.72f, 0.58f, 0.32f), col3(0.82f, 0.78f, 0.66f));
	mesh_beam(T(0.33f, D + 1.09f, 0), T(0.33f, D + 1.25f, 0), 0.02f, 0.04f, col3(0.72f, 0.58f, 0.32f));
	mesh_beam(T(-0.33f, D + 1.09f, 0), T(-0.33f, D + 1.25f, 0), 0.02f, 0.04f, col3(0.72f, 0.58f, 0.32f));
	/* Cot along the west wall, with a wool blanket. */
	u32 wool = col3(0.58f, 0.30f, 0.26f);
	mesh_box(T(-1.55f, D + 0.4f, -0.3f), v3_make(0.42f, 0.04f, 1.0f), mid);
	for (int k = 0; k < 4; k++)
		mesh_beam(T(-1.55f + ((k & 1) ? 0.38f : -0.38f), D, -0.3f + ((k & 2) ? 0.95f : -0.95f)),
		          T(-1.55f + ((k & 1) ? 0.38f : -0.38f), D + 0.4f, -0.3f + ((k & 2) ? 0.95f : -0.95f)), 0.05f, 0.05f, dark);
	mesh_box(T(-1.55f, D + 0.5f, -0.2f), v3_make(0.4f, 0.06f, 0.85f), wool);
	mesh_box(T(-1.55f, D + 0.53f, -1.05f), v3_make(0.3f, 0.07f, 0.16f), C_PAPER);
	mesh_box(T(-1.45f, D + 0.01f, 0.72f), v3_make(0.13f, 0.006f, 0.1f), col3(0.88f, 0.84f, 0.72f));   /* the map */
	/* Stove and kettle in the NE corner, flue through the roof. */
	mesh_prism(T(1.45f, D, -1.45f), T(1.45f, D + 0.75f, -1.45f), 0.3f, 10, C_IRON, col3(0.2f, 0.2f, 0.2f));
	mesh_prism(T(1.45f, D + 0.75f, -1.45f), T(1.45f, D + 2.5f, -1.45f), 0.07f, 6, C_IRON, C_IRON);
	mesh_prism(T(1.4f, D + 0.75f, -1.4f), T(1.4f, D + 0.95f, -1.4f), 0.12f, 8, col3(0.42f, 0.52f, 0.58f), col3(0.5f, 0.6f, 0.66f));
	mesh_beam(T(1.5f, D + 0.85f, -1.4f), T(1.62f, D + 0.93f, -1.4f), 0.03f, 0.03f, col3(0.42f, 0.52f, 0.58f));
	/* Radio table on the east wall: radio, handset, logbook, lamp. */
	mesh_box(T(1.62f, D + 0.78f, 0.35f), v3_make(0.34f, 0.025f, 0.6f), light);
	for (int k = 0; k < 4; k++)
		mesh_beam(T(1.62f + ((k & 1) ? 0.3f : -0.3f), D, 0.35f + ((k & 2) ? 0.55f : -0.55f)),
		          T(1.62f + ((k & 1) ? 0.3f : -0.3f), D + 0.78f, 0.35f + ((k & 2) ? 0.55f : -0.55f)), 0.05f, 0.05f, dark);
	mesh_box(T(1.75f, D + 0.91f, -0.1f), v3_make(0.14f, 0.11f, 0.18f), col3(0.30f, 0.36f, 0.30f));
	mesh_box(T(1.62f, D + 0.86f, -0.05f), v3_make(0.04f, 0.03f, 0.09f), col3(0.15f, 0.15f, 0.15f));
	mesh_box(T(1.6f, D + 0.82f, 0.42f), v3_make(0.12f, 0.016f, 0.16f), col3(0.48f, 0.20f, 0.18f));
	mesh_prism(T(1.7f, D + 0.8f, 0.82f), T(1.7f, D + 0.86f, 0.82f), 0.08f, 8, C_IRON, C_IRON);
	mesh_prism(T(1.7f, D + 0.86f, 0.82f), T(1.7f, D + 1.1f, 0.82f), 0.075f, 8, col3(0.98f, 0.82f, 0.56f), col3(0.98f, 0.82f, 0.56f));
	/* The lightning stool on its four glass insulator feet. */
	mesh_prism(T(-0.75f, D + 0.42f, -1.15f), T(-0.75f, D + 0.48f, -1.15f), 0.2f, 10, mid, light);
	for (int k = 0; k < 4; k++) {
		float a = LL_PI * 0.25f + k * LL_PI * 0.5f, ou = cosf(a) * 0.14f, ov = sinf(a) * 0.14f;
		mesh_beam(T(-0.75f + ou * 1.2f, D + 0.09f, -1.15f + ov * 1.2f), T(-0.75f + ou, D + 0.43f, -1.15f + ov), 0.04f, 0.04f, dark);
		mesh_prism(T(-0.75f + ou * 1.2f, D, -1.15f + ov * 1.2f), T(-0.75f + ou * 1.2f, D + 0.09f, -1.15f + ov * 1.2f),
		           0.055f, 8, col3(0.56f, 0.74f, 0.70f), col3(0.66f, 0.84f, 0.80f));
	}
	/* Augie's shelf under the north window: a few books and a jar. */
	mesh_box(T(-1.0f, D + 0.95f, -1.86f), v3_make(0.6f, 0.02f, 0.11f), mid);
	static const float books[5][4] = { { -1.45f, 0.30f, 0.32f, 0.36f }, { -1.33f, 0.52f, 0.42f, 0.30f },
	                                   { -1.22f, 0.36f, 0.46f, 0.50f }, { -1.1f, 0.62f, 0.56f, 0.40f },
	                                   { -0.98f, 0.40f, 0.30f, 0.26f } };
	for (int k = 0; k < 5; k++)
		mesh_box(T(books[k][0], D + 1.08f, -1.86f), v3_make(0.05f, 0.11f, 0.09f), col3(books[k][1], books[k][2], books[k][3]));
	mesh_prism(T(-0.6f, D + 0.97f, -1.86f), T(-0.6f, D + 1.15f, -1.86f), 0.06f, 8, col3(0.70f, 0.78f, 0.72f), col3(0.6f, 0.5f, 0.4f));
	/* The bird list, pinned to the north window frame. */
	mesh_box(T(0.6f, D + 1.5f, -1.95f), v3_make(0.1f, 0.13f, 0.005f), C_PAPER);
}

void build_tower_props(void)
{
	s_pad = world_pad_height();
	pg_begin(PG_TOWER, 360);
	frame();
	catwalk();
	cab();
	stairs();
	base();
	pg_end(PG_TOWER, T(0, 6.0f, 0), 13.0f);

	pg_begin(PG_TOWER_IN, 90);
	interior();
	pg_end(PG_TOWER_IN, T(0, TOWER_DECK_Y + 1.2f, 0), 3.5f);

	/* Binoculars on their hook by the door (gone once taken). */
	pg_begin(PG_BINOCULARS, 6);
	const float D = TOWER_DECK_Y, c = TOWER_CAB_HALF;
	mesh_beam(T(0.95f, D + 1.68f, c - 0.04f), T(0.95f, D + 1.68f, c - 0.12f), 0.02f, 0.02f, C_IRON);
	mesh_prism(T(0.9f, D + 1.48f, c - 0.1f), T(0.9f, D + 1.63f, c - 0.1f), 0.035f, 8, col3(0.16f, 0.16f, 0.15f), C_IRON);
	mesh_prism(T(1.0f, D + 1.48f, c - 0.1f), T(1.0f, D + 1.63f, c - 0.1f), 0.035f, 8, col3(0.16f, 0.16f, 0.15f), C_IRON);
	pg_end(PG_BINOCULARS, T(0.95f, D + 1.55f, c - 0.1f), 0.4f);
}
