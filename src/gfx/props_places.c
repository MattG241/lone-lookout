/*
 * props_places.c -- everything hand-built away from the tower: Pell's Cabin
 * (with its porch, spoon chimes and the room inside), the tarn dock and its
 * tin box, Augie's bench and the cairn on Cinder Ridge, the meadow's drift
 * fence and fallen log, the cold spring, the tagged snag and a fallen snag
 * in the burn, the trailhead (supply box, sign, road gate) and footbridges
 * where the trails cross the creek.
 *
 * Positions come from world/sites.h, shared with collision and interactables.
 */
#include <math.h>
#include "gfx/props_int.h"
#include "core/noise.h"

static float s_floor;

/* Cabin-local to world: u east, v south, y above the cabin floor. */
static v3 C(float u, float y, float v) { return v3_make(CABIN_X + u, s_floor + y, CABIN_Z + v); }

/* Unit vectors for a bearing: ahead (where it faces) and to its right. */
static v3 ahead(float b) { return v3_make(sinf(b), 0, -cosf(b)); }
static v3 right_of(float b) { return v3_make(cosf(b), 0, sinf(b)); }
static v3 on_ground(float x, float z, float up) { return v3_make(x, world_height(x, z) + up, z); }
static v3 offs(v3 p, v3 dir, float k) { return v3_add(p, v3_scale(dir, k)); }

/* A planar quad seen from both sides: the top face uses 'top', underneath 'under'. */
static void two_sided(v3 a, v3 b, v3 c, v3 d, u32 top, u32 under)
{
	v3 n = v3_cross(v3_sub(b, a), v3_sub(d, a));
	if (n.y < 0.0f) {
		v3 t = b;
		b = d;
		d = t;
	}
	mesh_quad(a, b, c, d, top);
	v3 down = v3_make(0, -0.015f, 0);
	mesh_quad(v3_add(a, down), v3_add(d, down), v3_add(c, down), v3_add(b, down), under);
}

/* A triangle facing 'out' (its winding is fixed to suit). */
static void tri_facing(v3 a, v3 b, v3 c, v3 out, u32 col)
{
	v3 n = v3_cross(v3_sub(b, a), v3_sub(c, a));
	if (v3_dot(n, out) < 0.0f) mesh_tri(a, c, b, col);
	else mesh_tri(a, b, c, col);
}

/* ------------------------------------------------------------------------ */

static void cabin(void)
{
	const float hx = CABIN_HALF_X, hz = CABIN_HALF_Z, H = CABIN_WALL_H, t = 0.06f, dh = CABIN_DOOR_HALF;
	const float porch = hz + CABIN_PORCH_D;
	u32 wall = C_WEATHERED, post = col3(0.44f, 0.40f, 0.36f), floor_c = col3(0.60f, 0.52f, 0.44f);
	u32 roof = col3(0.42f, 0.42f, 0.44f), under = col3(0.40f, 0.35f, 0.31f);

	/* Piers and the floor platform (cabin plus porch). */
	for (int i = -1; i <= 1; i++)
		for (int j = 0; j < 3; j++) {
			float u = i * (hx - 0.15f), v = j == 0 ? -hz + 0.15f : (j == 1 ? hz : porch - 0.15f);
			mesh_beam(on_ground(CABIN_X + u, CABIN_Z + v, -0.4f), C(u, -0.1f, v), 0.2f, 0.2f, post);
		}
	mesh_box(C(0, -0.06f, (porch - hz) * 0.5f), v3_make(hx, 0.06f, (porch + hz) * 0.5f), floor_c);

	/* Walls: board panels, a window in the west wall, the door in the south wall. */
	mesh_box(C(0, H * 0.5f, -hz + t), v3_make(hx, H * 0.5f, t), wall);
	mesh_box(C(hx - t, H * 0.5f, 0), v3_make(t, H * 0.5f, hz), wall);
	mesh_box(C(-hx + t, 0.45f, 0), v3_make(t, 0.45f, hz), wall);
	mesh_box(C(-hx + t, (1.6f + H) * 0.5f, 0), v3_make(t, (H - 1.6f) * 0.5f, hz), wall);
	mesh_box(C(-hx + t, 1.25f, -1.25f), v3_make(t, 0.35f, 0.75f), wall);
	mesh_box(C(-hx + t, 1.25f, 1.25f), v3_make(t, 0.35f, 0.75f), wall);
	mesh_box(C(-(hx + dh) * 0.5f, H * 0.5f, hz - t), v3_make((hx - dh) * 0.5f, H * 0.5f, t), wall);
	mesh_box(C((hx + dh) * 0.5f, H * 0.5f, hz - t), v3_make((hx - dh) * 0.5f, H * 0.5f, t), wall);
	mesh_box(C(0, (2.0f + H) * 0.5f, hz - t), v3_make(dh, (H - 2.0f) * 0.5f, t), wall);
	/* Battens, corner posts, the window frame and the door (open, inward). */
	for (int k = -3; k <= 3; k++)
		mesh_box(C(k * 0.7f, H * 0.5f, -hz - 0.01f), v3_make(0.03f, H * 0.5f, 0.02f), post);
	for (int k = 0; k < 4; k++)
		mesh_beam(C((k & 1) ? hx : -hx, 0, (k & 2) ? hz : -hz), C((k & 1) ? hx : -hx, H, (k & 2) ? hz : -hz), 0.14f, 0.14f, post);
	mesh_beam(C(-hx - 0.01f, 0.88f, -0.55f), C(-hx - 0.01f, 0.88f, 0.55f), 0.08f, 0.06f, post);
	mesh_beam(C(-hx - 0.01f, 1.62f, -0.55f), C(-hx - 0.01f, 1.62f, 0.55f), 0.08f, 0.06f, post);
	mesh_beam(C(-hx - 0.01f, 0.88f, 0), C(-hx - 0.01f, 1.62f, 0), 0.05f, 0.05f, post);
	mesh_box(C(dh + 0.04f, 1.0f, hz - 0.5f), v3_make(0.03f, 1.0f, 0.45f), col3(0.50f, 0.44f, 0.38f));

	/* Gable roof (ridge east-west) and a shed roof over the porch. */
	float o = hx + 0.25f, eave = H - 0.05f, ridge = H + 1.05f;
	two_sided(C(-o, eave, -hz - 0.45f), C(o, eave, -hz - 0.45f), C(o, ridge, 0), C(-o, ridge, 0), roof, under);
	two_sided(C(-o, ridge, 0), C(o, ridge, 0), C(o, eave, hz + 0.45f), C(-o, eave, hz + 0.45f), roof, under);
	two_sided(C(-o, eave - 0.12f, hz + 0.3f), C(o, eave - 0.12f, hz + 0.3f), C(o, 1.95f, porch + 0.25f),
	          C(-o, 1.95f, porch + 0.25f), roof, under);
	for (int s = -1; s <= 1; s += 2) {
		tri_facing(C(s * hx, H, -hz), C(s * hx, H, hz), C(s * hx, ridge - 0.05f, 0), v3_make((float)s, 0, 0), wall);
		tri_facing(C(s * hx, H, -hz), C(s * hx, ridge - 0.05f, 0), C(s * hx, H, hz), v3_make((float)-s, 0, 0), wall);
	}
	mesh_prism(C(-1.5f, ridge - 0.6f, -0.9f), C(-1.5f, ridge + 0.55f, -0.9f), 0.08f, 6, C_IRON, C_IRON);
	/* Porch posts and the spoon chimes hanging at the SE corner. */
	for (int s = -1; s <= 1; s += 2)
		mesh_beam(C(s * (hx - 0.12f), 0, porch - 0.12f), C(s * (hx - 0.12f), 1.95f, porch - 0.12f), 0.12f, 0.12f, post);
	mesh_prism(C(hx - 0.35f, 1.92f, porch - 0.3f), C(hx - 0.35f, 1.96f, porch - 0.3f), 0.1f, 8, C_IRON, C_IRON);
	for (int k = 0; k < 5; k++) {
		float a = k * LL_TAU / 5.0f, cu = cosf(a) * 0.07f, cv = sinf(a) * 0.07f;
		mesh_beam(C(hx - 0.35f + cu, 1.92f, porch - 0.3f + cv), C(hx - 0.35f + cu, 1.6f - 0.04f * k, porch - 0.3f + cv),
		          0.015f, 0.025f, col3(0.74f, 0.74f, 0.72f));
	}
	/* Rain barrel by the porch steps. */
	v3 bb = on_ground(CABIN_X - hx - 0.5f, CABIN_Z + porch - 0.4f, -0.05f);
	mesh_prism(bb, v3_add(bb, v3_make(0, 0.9f, 0)), 0.3f, 10, col3(0.45f, 0.36f, 0.28f), col3(0.2f, 0.24f, 0.26f));
}

static void cabin_inside(void)
{
	u32 wood_l = col3(0.66f, 0.56f, 0.44f), wood_d = col3(0.42f, 0.34f, 0.28f);
	/* Augie's table (NE) with the drawer, the pressed-flower book and a chair. */
	mesh_box(C(1.5f, 0.78f, -1.2f), v3_make(0.75f, 0.03f, 0.5f), wood_l);
	for (int k = 0; k < 4; k++)
		mesh_beam(C(1.5f + ((k & 1) ? 0.68f : -0.68f), 0, -1.2f + ((k & 2) ? 0.43f : -0.43f)),
		          C(1.5f + ((k & 1) ? 0.68f : -0.68f), 0.77f, -1.2f + ((k & 2) ? 0.43f : -0.43f)), 0.06f, 0.06f, wood_d);
	mesh_box(C(1.5f, 0.66f, -0.72f), v3_make(0.3f, 0.07f, 0.02f), wood_d);
	mesh_box(C(1.75f, 0.64f, -0.69f), v3_make(0.03f, 0.02f, 0.02f), C_IRON);
	mesh_box(C(1.25f, 0.83f, -1.25f), v3_make(0.13f, 0.025f, 0.17f), col3(0.36f, 0.46f, 0.36f));
	mesh_box(C(1.3f, 0.45f, -0.3f), v3_make(0.2f, 0.025f, 0.2f), wood_l);
	for (int k = 0; k < 4; k++)
		mesh_beam(C(1.3f + ((k & 1) ? 0.17f : -0.17f), 0, -0.3f + ((k & 2) ? 0.17f : -0.17f)),
		          C(1.3f + ((k & 1) ? 0.17f : -0.17f), 0.45f, -0.3f + ((k & 2) ? 0.17f : -0.17f)), 0.04f, 0.04f, wood_d);
	mesh_box(C(1.3f, 0.75f, -0.1f), v3_make(0.2f, 0.28f, 0.02f), wood_l);
	/* A bunk on the west wall and a shelf of jars on the north wall. */
	mesh_box(C(-1.95f, 0.45f, -0.4f), v3_make(0.42f, 0.04f, 1.0f), wood_l);
	mesh_box(C(-1.95f, 0.53f, -0.35f), v3_make(0.4f, 0.05f, 0.9f), col3(0.46f, 0.50f, 0.56f));
	for (int k = 0; k < 4; k++)
		mesh_beam(C(-1.95f + ((k & 1) ? 0.38f : -0.38f), 0, -0.4f + ((k & 2) ? 0.95f : -0.95f)),
		          C(-1.95f + ((k & 1) ? 0.38f : -0.38f), 0.45f, -0.4f + ((k & 2) ? 0.95f : -0.95f)), 0.05f, 0.05f, wood_d);
	mesh_box(C(-0.6f, 1.4f, -1.8f), v3_make(0.7f, 0.02f, 0.12f), wood_l);
	for (int k = 0; k < 5; k++)
		mesh_prism(C(-1.15f + k * 0.27f, 1.42f, -1.8f), C(-1.15f + k * 0.27f, 1.6f - (k & 1) * 0.05f, -1.8f), 0.06f, 8,
		           col3(0.62f + 0.05f * k, 0.70f, 0.62f), col3(0.55f, 0.45f, 0.35f));
	mesh_box(C(0, 0.004f, 0.4f), v3_make(0.8f, 0.004f, 0.55f), col3(0.55f, 0.36f, 0.30f));   /* rag rug */
}

static void dock(void)
{
	float x0 = world_dock_x0(), x1 = x0 - DOCK_LEN, y = DOCK_DECK_Y, z = DOCK_Z;
	u32 pile = col3(0.36f, 0.32f, 0.28f);
	int planks = 15;
	for (int k = 0; k < planks; k++) {
		float cx = x0 - (k + 0.5f) * DOCK_LEN / planks;
		float shade = 0.92f + 0.12f * ((hash32((u32)k * 2654435761u) & 255) / 255.0f);
		mesh_box(v3_make(cx, y - 0.04f, z), v3_make(DOCK_LEN / planks * 0.5f - 0.02f, 0.04f, DOCK_HALF_W),
		         rgba_u32(rgb_scale(rgb_make(0.58f, 0.52f, 0.46f), shade), 1.0f));
	}
	for (int s = -1; s <= 1; s += 2)
		mesh_beam(v3_make(x0, y - 0.14f, z + s * 0.6f), v3_make(x1, y - 0.14f, z + s * 0.6f), 0.1f, 0.12f, pile);
	for (int k = 0; k < 4; k++) {
		float px = x0 - 1.5f - k * 2.5f;
		if (k == 3) px = x1 + 0.12f;
		for (int s = -1; s <= 1; s += 2) {
			float pz = z + s * (DOCK_HALF_W - 0.06f);
			mesh_prism(on_ground(px, pz, -0.4f), v3_make(px, y + 0.25f, pz), 0.09f, 6, pile, col3(0.5f, 0.45f, 0.4f));
		}
	}
	/* Augie's tin box at the end. */
	mesh_box(v3_make(x1 + 0.7f, y + 0.08f, z + 0.5f), v3_make(0.14f, 0.08f, 0.1f), col3(0.56f, 0.33f, 0.22f));
}

static void ridge(void)
{
	float F = BENCH_FACING;
	v3 fwd = ahead(F), rt = right_of(F);
	v3 c = on_ground(BENCH_X, BENCH_Z, 0.0f);
	u32 seat = C_GREY_WOOD, leg = col3(0.48f, 0.45f, 0.42f);
	mesh_box_yaw(v3_add(c, v3_make(0, 0.45f, 0)), v3_make(0.95f, 0.035f, 0.2f), F, seat);
	v3 back = offs(c, fwd, -0.24f);
	mesh_box_yaw(v3_add(back, v3_make(0, 0.8f, 0)), v3_make(0.95f, 0.15f, 0.03f), F, seat);
	/* The carving: SIT HERE AT SEVEN, a paler gouge on the backrest. */
	mesh_box_yaw(v3_add(offs(back, fwd, 0.035f), v3_make(0, 0.82f, 0)), v3_make(0.5f, 0.05f, 0.005f), F,
	             col3(0.80f, 0.74f, 0.64f));
	for (int s = -1; s <= 1; s += 2) {
		v3 l = offs(c, rt, s * 0.75f);
		mesh_box_yaw(v3_add(l, v3_make(0, 0.22f, 0)), v3_make(0.05f, 0.25f, 0.18f), F, leg);
		v3 lb = offs(back, rt, s * 0.75f);
		mesh_beam(v3_add(lb, v3_make(0, 0.4f, 0)), v3_add(lb, v3_make(0, 0.96f, 0)), 0.06f, 0.06f, leg);
	}
	/* The cairn: stones stacked by everyone who ever came up. */
	v3 k = on_ground(CAIRN_X, CAIRN_Z, 0.0f);
	float y = k.y - 0.1f;
	for (int i = 0; i < 6; i++) {
		float s = 0.55f - i * 0.08f, hh = 0.13f + 0.03f * (i & 1);
		mesh_box_yaw(v3_make(k.x + 0.04f * (i & 1), y + hh, k.z - 0.03f * (i & 2)), v3_make(s, hh, s * 0.8f),
		             i * 0.9f, rgba_u32(rgb_scale(rgb_from(ALB_ROCK), 0.85f + 0.06f * (i % 3)), 1.0f));
		y += hh * 2.0f - 0.02f;
	}
}

static void meadow(void)
{
	u32 grey = C_GREY_WOOD, dark = col3(0.50f, 0.47f, 0.44f);
	v3 posts[FENCE_POSTS];
	for (int k = 0; k < FENCE_POSTS; k++) {
		float t = (float)k / (FENCE_POSTS - 1);
		float px = lerpf(FENCE_AX, FENCE_BX, t), pz = lerpf(FENCE_AZ, FENCE_BZ, t);
		u32 h = hash32((u32)k * 97u + 5u);
		float lean_x = ((h & 255) / 255.0f - 0.5f) * 0.25f, lean_z = (((h >> 8) & 255) / 255.0f - 0.5f) * 0.25f;
		v3 b = on_ground(px, pz, -0.3f);
		posts[k] = v3_make(px + lean_x, b.y + 1.6f, pz + lean_z);
		mesh_beam(b, posts[k], 0.12f, 0.12f, grey);
	}
	for (int k = 0; k + 1 < FENCE_POSTS; k++) {
		u32 h = hash32((u32)k * 131u + 9u);
		for (int r = 0; r < 2; r++) {
			float drop = r ? 0.45f : 0.95f;
			v3 a = v3_sub(posts[k], v3_make(0, drop, 0)), b = v3_sub(posts[k + 1], v3_make(0, drop, 0));
			if (((h >> r) & 3) == 0) {
				/* This rail is down: lying in the grass, one end still up. */
				b = on_ground(b.x - 0.3f, b.z + 0.4f, 0.06f);
			}
			mesh_beam(a, b, 0.08f, 0.1f, dark);
		}
	}
	/* The fallen log, its length across where you face when sitting on it. */
	v3 rt = right_of(LOG_FACING);
	v3 a = on_ground(LOG_X - rt.x * 1.6f, LOG_Z - rt.z * 1.6f, 0.27f);
	v3 b = on_ground(LOG_X + rt.x * 1.6f, LOG_Z + rt.z * 1.6f, 0.27f);
	mesh_prism(a, b, 0.29f, 8, C_BARK, C_CUT);
}

static void spring(void)
{
	float h = world_height(SPRING_X, SPRING_Z);
	u32 water = rgba_u32(rgb_lerp(rgb_from(ALB_WATER), rgb_make(0.3f, 0.36f, 0.4f), 0.4f), 1.0f);
	mesh_box(v3_make(SPRING_X, h + 0.25f, SPRING_Z - 1.45f), v3_make(0.85f, 0.5f, 0.35f), C_ROCK);
	mesh_prism(v3_make(SPRING_X, h + 0.5f, SPRING_Z - 1.5f), v3_make(SPRING_X, h + 0.56f, SPRING_Z - 0.85f), 0.035f, 6,
	           C_IRON, col3(0.1f, 0.1f, 0.1f));
	mesh_box(v3_make(SPRING_X, h + 0.27f, SPRING_Z - 0.6f), v3_make(0.7f, 0.27f, 0.35f), C_WEATHERED);
	mesh_box(v3_make(SPRING_X, h + 0.53f, SPRING_Z - 0.6f), v3_make(0.62f, 0.012f, 0.27f), water);
	mesh_beam(v3_make(SPRING_X + 0.95f, h - 0.3f, SPRING_Z - 0.6f), v3_make(SPRING_X + 0.95f, h + 1.25f, SPRING_Z - 0.6f),
	          0.1f, 0.1f, C_GREY_WOOD);
	mesh_prism(v3_make(SPRING_X + 1.04f, h + 0.94f, SPRING_Z - 0.5f), v3_make(SPRING_X + 1.04f, h + 1.04f, SPRING_Z - 0.5f),
	           0.04f, 8, col3(0.70f, 0.71f, 0.72f), col3(0.5f, 0.5f, 0.5f));
	mesh_box(v3_make(SPRING_X + 0.93f, h + 1.0f, SPRING_Z - 0.54f), v3_make(0.07f, 0.09f, 0.005f), C_PAPER);
}

static void burn(void)
{
	u32 silver = rgba_u32(rgb_scale(rgb_from(ALB_BURN_SNAG), 1.12f), 1.0f);
	u32 char_c = col3(0.30f, 0.28f, 0.27f);
	/* The tagged snag: a tall silver trunk with a broken top and stubs. */
	v3 b = on_ground(TAG_SNAG_X, TAG_SNAG_Z, -0.3f);
	v3 top = v3_add(b, v3_make(0.15f, 7.8f, -0.1f));
	mesh_prism(b, top, 0.24f, 8, silver, char_c);
	mesh_prism(top, v3_add(top, v3_make(0.3f, 0.9f, 0.1f)), 0.12f, 6, silver, silver);
	for (int k = 0; k < 6; k++) {
		u32 hsh = hash32((u32)k * 7u + 1u);
		float a = (hsh & 1023) / 1023.0f * LL_TAU, y = 2.6f + k * 0.85f, l = 0.4f + ((hsh >> 10) & 255) / 255.0f * 0.6f;
		v3 p = v3_add(b, v3_make(0.15f * y / 7.8f, y, -0.1f * y / 7.8f));
		mesh_beam(p, v3_add(p, v3_make(cosf(a) * l, l * 0.35f, sinf(a) * l)), 0.06f, 0.06f, silver);
	}
	/* The aluminium tag, nailed on at eye height, facing the trail (SW). */
	v3 tag = on_ground(TAG_SNAG_X - 0.17f, TAG_SNAG_Z + 0.17f, 1.45f);
	mesh_box_yaw(tag, v3_make(0.06f, 0.04f, 0.004f), BEARING(225.0f), col3(0.76f, 0.77f, 0.79f));
	/* A fallen snag to sit on, its root plate tipped up at one end. */
	v3 rt = right_of(FALLEN_FACING);
	v3 a = on_ground(FALLEN_X - rt.x * 2.6f, FALLEN_Z - rt.z * 2.6f, 0.3f);
	v3 e = on_ground(FALLEN_X + rt.x * 2.6f, FALLEN_Z + rt.z * 2.6f, 0.3f);
	mesh_prism(a, e, 0.27f, 8, silver, char_c);
	mesh_prism(v3_add(e, v3_scale(rt, 0.1f)), v3_add(e, v3_scale(rt, 0.3f)), 0.75f, 9, col3(0.40f, 0.34f, 0.28f), char_c);
}

static void trailhead(void)
{
	/* Bear-proof supply box. */
	v3 sb = on_ground(SUPPLY_X, SUPPLY_Z, 0.0f);
	u32 green = col3(0.30f, 0.38f, 0.31f);
	mesh_box_yaw(v3_add(sb, v3_make(0, 0.45f, 0)), v3_make(0.6f, 0.45f, 0.4f), BEARING(90.0f), green);
	mesh_box_yaw(v3_add(sb, v3_make(0, 0.92f, 0)), v3_make(0.63f, 0.03f, 0.43f), BEARING(90.0f), col3(0.24f, 0.30f, 0.25f));
	mesh_box(v3_add(sb, v3_make(0.42f, 0.72f, 0)), v3_make(0.02f, 0.06f, 0.05f), C_IRON);
	/* Trail sign. */
	v3 sg = on_ground(SIGN_X, SIGN_Z, -0.3f);
	mesh_beam(sg, v3_add(sg, v3_make(0, 2.3f, 0)), 0.12f, 0.12f, C_GREY_WOOD);
	mesh_box_yaw(v3_add(sg, v3_make(-0.08f, 2.05f, 0)), v3_make(0.5f, 0.17f, 0.03f), BEARING(270.0f), col3(0.52f, 0.40f, 0.30f));
	mesh_box_yaw(v3_add(sg, v3_make(-0.115f, 2.05f, 0)), v3_make(0.4f, 0.09f, 0.004f), BEARING(270.0f), col3(0.86f, 0.80f, 0.66f));
	/* Road gate: two posts and a swing bar; fence wire either side. */
	u32 bar = col3(0.72f, 0.60f, 0.30f);
	for (int s = -1; s <= 1; s += 2) {
		v3 p = on_ground(GATE_X + s * 2.4f, GATE_Z, -0.3f);
		mesh_prism(p, v3_add(p, v3_make(0, 1.6f, 0)), 0.12f, 8, bar, bar);
	}
	v3 gl = on_ground(GATE_X - 2.4f, GATE_Z, 1.0f), gr = on_ground(GATE_X + 2.4f, GATE_Z, 1.0f);
	mesh_prism(gl, v3_make(gr.x, gl.y, gr.z), 0.05f, 6, bar, bar);
	for (int s = -1; s <= 1; s += 2) {
		v3 prev = { 0 };
		for (int k = 0; k <= 4; k++) {
			float x = GATE_X + s * (2.4f + k * (GATE_HALF - 2.4f) / 4.0f);
			v3 p = on_ground(x, GATE_Z, -0.2f);
			mesh_beam(p, v3_add(p, v3_make(0, 1.4f, 0)), 0.1f, 0.1f, C_GREY_WOOD);
			if (k > 0)
				for (int w = 0; w < 2; w++) {
					float y = 0.7f + w * 0.5f;
					mesh_beam(v3_add(prev, v3_make(0, y, 0)), v3_add(p, v3_make(0, y, 0)), 0.015f, 0.015f, C_IRON);
				}
			prev = p;
		}
	}
}

static void bridges(void)
{
	const float (*br)[3];
	int n = world_bridges(&br);
	for (int i = 0; i < n; i++) {
		float x = br[i][0], z = br[i][1], yaw = br[i][2];
		v3 c = on_ground(x, z, 0.1f), f = ahead(yaw), r = right_of(yaw);
		mesh_box_yaw(c, v3_make(0.8f, 0.06f, 1.9f), yaw, col3(0.55f, 0.48f, 0.40f));
		for (int s = -1; s <= 1; s += 2) {
			v3 a = offs(offs(c, f, -2.0f), r, s * 0.82f), b = offs(offs(c, f, 2.0f), r, s * 0.82f);
			mesh_prism(v3_add(a, v3_make(0, 0.05f, 0)), v3_add(b, v3_make(0, 0.05f, 0)), 0.11f, 6, C_BARK, C_CUT);
		}
	}
}

void build_place_props(void)
{
	s_floor = world_cabin_floor();
	pg_begin(PG_CABIN, 110);
	cabin();
	pg_end(PG_CABIN, C(0, 1.5f, 0.8f), 6.5f);
	pg_begin(PG_CABIN_IN, 60);
	cabin_inside();
	pg_end(PG_CABIN_IN, C(0, 1.0f, 0), 3.5f);

	pg_begin(PG_TARN, 40);
	dock();
	pg_end(PG_TARN, v3_make(world_dock_x0() - DOCK_LEN * 0.5f, DOCK_DECK_Y, DOCK_Z), 6.0f);

	pg_begin(PG_RIDGE, 30);
	ridge();
	pg_end(PG_RIDGE, on_ground((BENCH_X + CAIRN_X) * 0.5f, (BENCH_Z + CAIRN_Z) * 0.5f, 1.0f), 9.0f);

	pg_begin(PG_MEADOW, 50);
	meadow();
	pg_end(PG_MEADOW, on_ground((FENCE_AX + LOG_X) * 0.5f, (FENCE_AZ + LOG_Z) * 0.5f, 1.0f), 40.0f);

	pg_begin(PG_SPRING, 16);
	spring();
	pg_end(PG_SPRING, on_ground(SPRING_X, SPRING_Z, 0.6f), 3.0f);

	pg_begin(PG_BURN, 30);
	burn();
	pg_end(PG_BURN, on_ground((TAG_SNAG_X + FALLEN_X) * 0.5f, (TAG_SNAG_Z + FALLEN_Z) * 0.5f, 3.0f), 16.0f);

	pg_begin(PG_TRAILHEAD, 50);
	trailhead();
	pg_end(PG_TRAILHEAD, on_ground(GATE_X, (SUPPLY_Z + GATE_Z) * 0.5f, 1.0f), 20.0f);

	const float (*br)[3];
	int nb = world_bridges(&br);
	pg_begin(PG_BRIDGES, 12 * (nb ? nb : 1));
	bridges();
	pg_end(PG_BRIDGES, v3_make(256, 40, 256), nb ? 400.0f : 0.0f);
}
