/*
 * test_world.c -- host-side tests for the walkable world and murmur timing.
 *
 * Built and run by `make test` with the system C compiler (no Wii needed).
 * These exercise the same C files the game links, against the gctypes stub.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "world/world.h"
#include "audio/voice.h"

static int s_fails;

#define CHECK(cond, ...) do { \
	if (!(cond)) { s_fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* One fixed simulation step of walking, mirroring game.c's update_walk. */
static v3 step(v3 p, float dx, float dz)
{
	surface_t s;
	v3 to = v3_make(p.x + dx, p.y, p.z + dz);
	to = world_move(p, to, p.y);
	float g = world_ground(to.x, to.z, p.y, &s);
	to.y = (g >= p.y - 0.65f) ? g : p.y - 0.05f;
	return to;
}

static void test_climb_stairs(void)
{
	float pad = world_pad_height();
	v3 p = v3_make(STAIR_X, 0.0f, TOWER_Z + TOWER_DECK_HALF + STAIR_RUN + 2.5f);
	p.y = world_height(p.x, p.z);
	for (int i = 0; i < 60 * 12; i++)                 /* 12 s north at walking pace */
		p = step(p, 0.0f, -2.2f / 60.0f);
	CHECK(fabsf(p.y - (pad + TOWER_DECK_Y)) < 0.05f, "expected deck height %.2f, got %.2f", pad + TOWER_DECK_Y, p.y);

	/* Railing: pushing west along the deck must keep us on it. */
	for (int i = 0; i < 60 * 4; i++)
		p = step(p, -2.2f / 60.0f, 0.0f);
	CHECK(p.x >= TOWER_X - TOWER_DECK_HALF, "walked off the deck to x=%.2f", p.x);
	CHECK(fabsf(p.y - (pad + TOWER_DECK_Y)) < 0.05f, "fell off the deck (y=%.2f)", p.y);
}

static void test_leg_collision(void)
{
	float lx = TOWER_X - TOWER_LEG_BASE, lz = TOWER_Z - TOWER_LEG_BASE;
	v3 q = v3_make(lx + 0.1f, 0.0f, lz);
	q.y = world_height(q.x, q.z);                       /* on the ground, under the deck */
	v3 r = world_move(q, q, q.y);
	float d = sqrtf((r.x - lx) * (r.x - lx) + (r.z - lz) * (r.z - lz));
	CHECK(d >= 0.5f, "tower leg did not push the player out (d=%.2f)", d);
}

static void test_world_edge(void)
{
	v3 p = v3_make(0.0f, 0.0f, 60.0f);
	p.y = world_height(p.x, p.z);
	for (int i = 0; i < 60 * 30; i++)                  /* 30 s walking south */
		p = step(p, 0.0f, 3.4f / 60.0f);
	CHECK(sqrtf(p.x * p.x + p.z * p.z) <= WORLD_WALK_RADIUS + 0.01f, "escaped the bowl (r=%.2f)",
	      sqrtf(p.x * p.x + p.z * p.z));
}

static void test_heights_sane(void)
{
	for (float z = -WORLD_HALF; z <= WORLD_HALF; z += 7.3f)
		for (float x = -WORLD_HALF; x <= WORLD_HALF; x += 7.3f) {
			float h = world_height(x, z);
			CHECK(h == h && h > -20.0f && h < 80.0f, "height out of range at (%.1f, %.1f): %.2f", x, z, h);
		}
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
	test_climb_stairs();
	test_leg_collision();
	test_world_edge();
	test_voice_timing();
	printf(s_fails ? "%d check(s) failed\n" : "all host tests passed\n", s_fails);
	return s_fails ? 1 : 0;
}
