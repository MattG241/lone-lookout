/*
 * test_world.c -- host-side tests for the walkable Ashen Peaks and murmur timing.
 *
 * Built and run by `make test` with the system C compiler (no Wii needed).
 * They exercise the same C files the game links, against the baked data in
 * gen/ and the gctypes stub, by "walking" the way game.c does: fixed 60 Hz
 * steps through world_move() and world_ground().
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "world/world.h"
#include "world/sites.h"
#include "audio/voice.h"

static int s_fails, s_checks;

#define CHECK(cond, ...) do { \
	s_checks++; \
	if (!(cond)) { s_fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

#define DT (1.0f / 60.0f)
#define SPEED 2.2f

typedef struct {
	v3 p;
	float vy;
	int edge;                 /* last boundary that stopped us */
} walker;

static walker at(float x, float z)
{
	walker w = { v3_make(x, 0, z), 0.0f, 0 };
	w.p.y = world_ground(x, z, 1e9f, NULL);
	return w;
}

/* One fixed simulation step, mirroring game.c's update_walk. */
static void step(walker *w, float dx, float dz)
{
	v3 to = v3_make(w->p.x + dx, w->p.y, w->p.z + dz);
	int edge = 0;
	to = world_move(w->p, to, w->p.y, &edge);
	if (edge) w->edge = edge;
	float g = world_ground(to.x, to.z, w->p.y, NULL);
	if (g >= w->p.y - 0.65f) {
		to.y = g;
		w->vy = 0.0f;
	} else {
		w->vy -= 9.8f * DT;
		to.y = fmaxf(g, w->p.y + w->vy * DT);
	}
	w->p = to;
}

static float dist2(v3 p, float x, float z) { return sqrtf((p.x - x) * (p.x - x) + (p.z - z) * (p.z - z)); }

/* Walk straight at a point; true if we got within 0.35 m before stalling. */
static bool walk_to(walker *w, float x, float z, float max_s)
{
	float best = 1e9f, stall = 0.0f;
	for (int i = 0; i < (int)(max_s * 60.0f); i++) {
		float d = dist2(w->p, x, z);
		if (d < 0.35f) return true;
		float s = fminf(SPEED * DT, d);
		step(w, (x - w->p.x) / d * s, (z - w->p.z) / d * s);
		if (d < best - 0.01f) {
			best = d;
			stall = 0.0f;
		} else if ((stall += DT) > 2.0f) {
			return false;
		}
	}
	return false;
}

static void test_heights_sane(void)
{
	for (float z = 0.0f; z <= WORLD_SIZE; z += 7.3f)
		for (float x = 0.0f; x <= WORLD_SIZE; x += 7.3f) {
			float h = world_height(x, z);
			CHECK(h == h && h > 0.0f && h < 260.0f, "height out of range at (%.1f, %.1f): %.2f", x, z, h);
		}
	/* Mesh-exact: matches the grid at vertices and is continuous across the diagonal. */
	CHECK(fabsf(world_height(64.0f, 128.0f) - world_grid_height(16, 32)) < 1e-4f, "height at a vertex");
	float a = world_height(101.999f, 2.001f), b = world_height(102.001f, 1.999f);
	CHECK(fabsf(a - b) < 0.01f, "seam across the cell diagonal (%.3f vs %.3f)", a, b);
}

static void test_trails_walkable(void)
{
	for (int t = 0; t < world_trail_count(); t++) {
		int n;
		const float (*pts)[2] = world_trail(t, &n);
		walker w = at(pts[0][0], pts[0][1]);
		bool ok = true;
		for (int k = 1; k < n && ok; k++) {
			/* Waypoints every ~3 m along the segment. */
			float dx = pts[k][0] - pts[k - 1][0], dz = pts[k][1] - pts[k - 1][1];
			int sub = (int)(sqrtf(dx * dx + dz * dz) / 3.0f) + 1;
			for (int s = 1; s <= sub && ok; s++) {
				float x = pts[k - 1][0] + dx * s / sub, z = pts[k - 1][1] + dz * s / sub;
				ok = walk_to(&w, x, z, 30.0f);
				if (!ok)
					printf("  trail %d stalled near (%.1f, %.1f) heading for (%.1f, %.1f), edge %d\n",
					       t, w.p.x, w.p.z, x, z, w.edge);
			}
		}
		CHECK(ok, "trail %d (%d points) is not walkable end to end", t, n);
	}
}

static void test_tower(void)
{
	const float X = TOWER_X, Z = TOWER_Z, pad = world_pad_height(), deck = pad + TOWER_DECK_Y;
	const float m = (STAIR_IN + STAIR_OUT) * 0.5f;
	walker w = at(X, Z + 12.0f);
	CHECK(walk_to(&w, X - m - 0.2f, Z + m, 20), "reach the foot of the stairs");
	CHECK(walk_to(&w, X + m, Z + m, 20), "flight A to the SE landing");
	CHECK(fabsf(w.p.y - (pad + STAIR_RISE)) < 0.05f, "SE landing height %.2f", w.p.y - pad);
	CHECK(walk_to(&w, X + m, Z - m, 20), "flight B to the NE landing");
	CHECK(walk_to(&w, X - m, Z - m, 20), "flight C to the NW landing");
	CHECK(walk_to(&w, X - m, Z + 3.0f, 20), "flight D to the top landing");
	CHECK(fabsf(w.p.y - deck) < 0.05f, "top landing at deck height (got %.2f, want %.2f)", w.p.y, deck);
	CHECK(walk_to(&w, X - 2.4f, Z + 2.5f, 10), "step from the landing onto the deck");
	CHECK(walk_to(&w, X, Z + 2.6f, 10), "walk to the cab door");
	CHECK(walk_to(&w, X, Z + 0.9f, 10), "through the door into the cab");
	CHECK(world_indoor(w.p) == INDOOR_CAB, "inside the cab");
	CHECK(fabsf(w.p.y - deck) < 0.05f, "cab floor at deck height");

	/* The fire finder stands in the middle: walking straight at it, we go round, never through. */
	walker f = w;
	float closest = 1e9f;
	for (int i = 0; i < 120; i++) {
		step(&f, 0.0f, -SPEED * DT);
		closest = fminf(closest, dist2(f.p, X, Z));
	}
	CHECK(closest > 0.6f, "walked through the fire finder (%.2f m from its centre)", closest);

	/* Cab walls: from inside, the east wall holds. */
	walker e = w;
	walk_to(&e, X + 1.0f, Z + 1.3f, 5);
	walk_to(&e, X + 3.0f, Z + 1.3f, 5);
	CHECK(e.p.x < X + TOWER_CAB_HALF, "walked through the east cab wall (x=%.2f)", e.p.x - X);

	/* Railing: from the deck's north side, walking north stays on the deck. */
	walker r = w;
	r.p = v3_make(X + 2.5f, deck, Z - 2.5f);
	walk_to(&r, X + 2.5f, Z - 8.0f, 6);
	CHECK(fabsf(r.p.y - deck) < 0.05f && r.p.z > Z - TOWER_DECK_HALF - 0.01f,
	      "fell off the north side of the deck (y %.2f, v %.2f)", r.p.y - pad, r.p.z - Z);
	/* ...and the deck's west railing stops a short-cut onto flight D. */
	r.p = v3_make(X - 2.6f, deck, Z);
	walk_to(&r, X - 6.0f, Z, 6);
	CHECK(r.p.x > X - TOWER_DECK_HALF - 0.01f, "climbed over the west railing (u %.2f)", r.p.x - X);

	/* Walking into the low side of flight A from the south is a wall, not a climb. */
	walker s = at(X + 1.0f, Z + 7.0f);
	walk_to(&s, X + 1.0f, Z + 2.0f, 6);
	CHECK(s.p.z > Z + STAIR_OUT - 0.01f, "walked under or through flight A (v %.2f)", s.p.z - Z);

	/* Legs push the player out at ground level. */
	float lx = X - TOWER_LEG_BASE, lz = Z - TOWER_LEG_BASE;
	v3 q = v3_make(lx + 0.1f, pad, lz);
	v3 out = world_move(q, q, q.y, NULL);
	CHECK(dist2(out, lx, lz) >= 0.4f, "tower leg did not push the player out (d=%.2f)", dist2(out, lx, lz));

	/* The singing stair: third tread from the top of flight D. */
	float tz = Z + STAIR_TOP_V - (STAIR_TOP_V + STAIR_IN) * 2.5f / STAIR_STEPS;
	v3 tread = v3_make(X - m, world_ground(X - m, tz, deck, NULL), tz);
	CHECK(world_stair_tread(tread) == 3 * STAIR_STEPS + STAIR_STEPS - 3, "tread id %d", world_stair_tread(tread));
}

static void test_cabin(void)
{
	const float X = CABIN_X, Z = CABIN_Z, F = world_cabin_floor();
	walker w = at(X, Z + CABIN_HALF_Z + 6.0f);
	CHECK(walk_to(&w, X, Z + CABIN_HALF_Z + 0.8f, 10), "up onto the porch");
	CHECK(fabsf(w.p.y - (F - 0.03f)) < 0.05f, "porch height");
	CHECK(walk_to(&w, X, Z - 0.5f, 10), "through the cabin door");
	CHECK(world_indoor(w.p) == INDOOR_CABIN, "inside the cabin");
	walker e = at(X + CABIN_HALF_X + 3.0f, Z);
	walk_to(&e, X, Z, 6);
	CHECK(e.p.x > X + CABIN_HALF_X, "walked through the cabin's east wall (x=%.2f)", e.p.x - X);
}

static void test_tarn_and_dock(void)
{
	walker w = at(world_dock_x0() + 3.0f, DOCK_Z);
	CHECK(walk_to(&w, world_dock_x0() - DOCK_LEN + 0.5f, DOCK_Z, 15), "walk to the end of the dock");
	CHECK(fabsf(w.p.y - DOCK_DECK_Y) < 0.05f, "dock deck height (%.2f vs %.2f)", w.p.y, DOCK_DECK_Y);
	/* Step off the end: the water stops us. */
	walk_to(&w, world_dock_x0() - DOCK_LEN - 5.0f, DOCK_Z, 4);
	CHECK(w.p.x > world_dock_x0() - DOCK_LEN - 0.01f, "walked off the dock into the tarn");

	walker s = at(150.0f, 178.0f);                          /* where the meadow trail meets the shore */
	CHECK(world_height(s.p.x, s.p.z) > TARN_WATER_Y, "the shore trail is dry");
	walk_to(&s, TARN_X, TARN_Z, 30);
	CHECK(world_height(s.p.x, s.p.z) > TARN_WATER_Y - 0.3f, "waded into deep water (depth %.2f)",
	      TARN_WATER_Y - world_height(s.p.x, s.p.z));
	CHECK(s.edge == WORLD_EDGE_WATER, "the tarn reports the water edge (%d)", s.edge);
}

static void test_boundaries(void)
{
	walker g = at(START_X, START_Z);
	walk_to(&g, GATE_X, WORLD_SIZE - 1.0f, 20);
	CHECK(g.p.z < GATE_Z, "walked through the road gate (z=%.2f)", g.p.z);
	CHECK(g.edge == WORLD_EDGE_GATE, "the gate reports itself (%d)", g.edge);

	/* Into the rim in four directions from the tower: always stopped short of the map edge. */
	const float dirs[4][2] = { { 0, -1 }, { 1, 0 }, { -1, 0 }, { -0.7f, 0.7f } };
	for (int d = 0; d < 4; d++) {
		walker w = at(TOWER_X + dirs[d][0] * 20.0f, TOWER_Z + 20.0f + dirs[d][1] * 20.0f);
		walk_to(&w, 256.0f + dirs[d][0] * 300.0f, 256.0f + dirs[d][1] * 300.0f, 200);
		float r = sqrtf((w.p.x - 256) * (w.p.x - 256) + (w.p.z - 256) * (w.p.z - 256));
		CHECK(r < 250.0f, "direction %d escaped to r=%.1f (%.1f, %.1f)", d, r, w.p.x, w.p.z);
	}
}

static void test_places(void)
{
	int n;
	const interactable_t *ia = world_interactables(&n);
	CHECK(n >= 25, "only %d interactables", n);
	int notes = 0;
	for (int i = 0; i < n; i++) {
		if (ia[i].kind == IA_NOTE) notes++;
		CHECK(ia[i].pos.y == ia[i].pos.y && ia[i].pos.x > 0 && ia[i].pos.x < WORLD_SIZE, "interactable %d placed", i);
		if (ia[i].kind == IA_SIT) {
			/* Seated eye must be above the ground it sits on. */
			float g = world_ground(ia[i].sit_eye.x, ia[i].sit_eye.z, ia[i].sit_eye.y, NULL);
			CHECK(ia[i].sit_eye.y - g > 0.5f && ia[i].sit_eye.y - g < 1.6f, "seat %d eye %.2f above ground",
			      ia[i].id, ia[i].sit_eye.y - g);
		}
	}
	CHECK(notes == 11, "expected 11 placed notes (N12 arrives by post), got %d", notes);
	CHECK(world_area_at(TOWER_X, TOWER_Z) == AREA_TOWER, "tower area");
	CHECK(world_area_at(112, 334) == AREA_MEADOW, "meadow area");
	CHECK(world_area_at(50, 50) == -1, "no area in the corner");
}

static void test_foliage(void)
{
	int nt, nf;
	const tree_t *t = world_trees(&nt);
	world_tufts(&nf);
	CHECK(nt > 800 && nt < 2500, "tree count %d", nt);
	CHECK(nf > 3000 && nf < 20000, "tuft count %d", nf);
	int on_trail = 0;
	for (int i = 0; i < nt; i++)
		if (world_trail_dist(t[i].x, t[i].z) < 4.0f) on_trail++;
	CHECK(on_trail == 0, "%d trees stand on trails", on_trail);
}

static void test_voice_timing(void)
{
	float a = voice_line_seconds("Kestrel, Basin. Radio check, over.", SPEAKER_DOT);
	float b = voice_line_seconds("Kestrel, Basin. Radio check, over.", SPEAKER_DOT);
	CHECK(a == b, "voice timing must be deterministic (%.3f vs %.3f)", a, b);
	CHECK(a > 1.5f && a < 6.0f, "implausible line length %.2f s", a);
	float slow = voice_line_seconds("Kestrel, Basin. Radio check, over.", SPEAKER_THEO);
	CHECK(slow > a, "Theo should speak slower than Dot (%.2f vs %.2f)", slow, a);
}

int main(void)
{
	world_init();
	test_heights_sane();
	test_trails_walkable();
	test_tower();
	test_cabin();
	test_tarn_and_dock();
	test_boundaries();
	test_places();
	test_foliage();
	test_voice_timing();
	if (s_fails) printf("%d of %d check(s) failed\n", s_fails, s_checks);
	else printf("all %d host checks passed\n", s_checks);
	return s_fails ? 1 : 0;
}
