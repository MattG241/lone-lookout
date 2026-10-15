/*
 * places.c -- everything you can point at and press A on: Augie's notes,
 * the sit spots, and the things in the cab and at the cabin.
 *
 * Positions follow the props (gfx/props_*.c) through world/sites.h and the
 * structure constants in world.h.
 */
#include <math.h>
#include <string.h>
#include "world/world.h"
#include "world/world_int.h"
#include "world/sites.h"

#define MAX_IA 40
#define SEAT_EYE 0.80f               /* eye above the seat when sitting */

static interactable_t s_ia[MAX_IA];
static int s_n;

static interactable_t *add(ia_kind kind, int id, float x, float y, float z, float radius, const char *verb)
{
	interactable_t *ia = &s_ia[s_n++];
	memset(ia, 0, sizeof(*ia));
	ia->pos = v3_make(x, y, z);
	ia->radius = radius;
	ia->kind = kind;
	ia->id = id;
	ia->verb = verb;
	return ia;
}

static void add_note(int id, float x, float y, float z)
{
	add(IA_NOTE, id, x, y, z, 0.3f, id == NOTE_MAP ? "Look" : "Read");
}

/* A sit spot: the reticle target sits on the seat, the eye above it. */
static void add_seat(int id, float x, float seat_y, float z, float facing, float target_r)
{
	interactable_t *ia = add(IA_SIT, id, x, seat_y + 0.2f, z, target_r, "Sit");
	ia->sit_eye = v3_make(x, seat_y + SEAT_EYE, z);
	ia->sit_yaw = facing;
}

void places_init(void)
{
	s_n = 0;
	const float X = TOWER_X, Z = TOWER_Z, pad = world_pad_height(), D = pad + TOWER_DECK_Y;
	const float c = TOWER_CAB_HALF;

	/* --- Kestrel Lookout ------------------------------------------------ */
	add_note(NOTE_WELCOME, X - 1.0f, D + 0.72f, Z + c + 0.07f);           /* beside the door */
	add_note(NOTE_STAIR, X - STAIR_IN, pad + 1.2f, Z + STAIR_OUT + 0.12f); /* foot of the stairs */
	add_note(NOTE_MAP, X - 1.45f, D + 0.12f, Z + 0.72f);                   /* under the cot */
	add_note(NOTE_BIRDS, X + 0.6f, D + 1.5f, Z - c + 0.08f);               /* north window */
	add(IA_SLEEP, 0, X - 1.55f, D + 0.5f, Z - 0.5f, 0.55f, "Sleep");
	add(IA_KETTLE, 0, X + 1.45f, D + 0.98f, Z - 1.45f, 0.22f, "Brew");
	add(IA_HANDSET, 0, X + 1.66f, D + 0.9f, Z - 0.05f, 0.18f, "Call");
	add(IA_LOGBOOK, 0, X + 1.6f, D + 0.83f, Z + 0.42f, 0.2f, "Write");
	add(IA_LAMP, 0, X + 1.7f, D + 1.12f, Z + 0.82f, 0.18f, "Lamp");
	add(IA_FINDER, 0, X, D + 1.18f, Z, 0.4f, "Sight");
	add(IA_BINOCULARS, 0, X + 0.95f, D + 1.55f, Z + c - 0.08f, 0.16f, "Take");
	add(IA_WEATHER, 0, X + TOWER_DECK_HALF + 0.3f, D + 1.25f, Z - 1.6f, 0.35f, "Read");      /* instrument shelter */
	add_seat(0, X - 0.75f, D + 0.5f, Z - 1.15f, BEARING(292.0f), 0.3f);      /* the lightning stool */
	add_seat(1, X - 2.55f, D + 0.05f, Z - 1.4f, BEARING(262.0f), 0.4f);      /* the catwalk, facing west */

	/* --- Larkspur Meadow ------------------------------------------------- */
	{
		float t = (float)FENCE_NOTE_POST / (FENCE_POSTS - 1);
		float px = lerpf(FENCE_AX, FENCE_BX, t), pz = lerpf(FENCE_AZ, FENCE_BZ, t);
		add_note(NOTE_FENCE, px + 0.05f, world_height(px, pz) + 1.0f, pz + 0.13f);
		add_seat(2, LOG_X, world_height(LOG_X, LOG_Z) + 0.42f, LOG_Z, LOG_FACING, 0.9f);
	}

	/* --- Cold spring ----------------------------------------------------- */
	{
		float h = world_height(SPRING_X + 0.95f, SPRING_Z - 0.6f);
		add_note(NOTE_SPRING, SPRING_X + 0.95f, h + 1.0f, SPRING_Z - 0.48f);
	}

	/* --- Mirror Tarn ------------------------------------------------------ */
	{
		float x_end = world_dock_x0() - DOCK_LEN;
		add_note(NOTE_DOCK, x_end + 0.7f, DOCK_DECK_Y + 0.2f, DOCK_Z + 0.5f);
		add_seat(3, x_end + 0.35f, DOCK_DECK_Y + 0.05f, DOCK_Z - 0.2f, BEARING(208.0f), 0.45f);   /* down the tarn to the bowl */
	}

	/* --- Cinder Ridge ------------------------------------------------------ */
	{
		float hb = world_height(BENCH_X, BENCH_Z);
		/* The carving is on the backrest, behind the seat. */
		float bx = BENCH_X - sinf(BENCH_FACING) * 0.24f, bz = BENCH_Z + cosf(BENCH_FACING) * 0.24f;
		add_note(NOTE_BENCH, bx, hb + 0.82f, bz);
		add_seat(4, BENCH_X + sinf(BENCH_FACING) * 0.05f, hb + 0.45f, BENCH_Z - cosf(BENCH_FACING) * 0.05f,
		         BENCH_FACING, 0.5f);
	}

	/* --- The '98 Burn ------------------------------------------------------ */
	{
		float h = world_height(TAG_SNAG_X, TAG_SNAG_Z);
		add_note(NOTE_SNAG, TAG_SNAG_X - 0.2f, h + 1.45f, TAG_SNAG_Z + 0.2f);
		add_seat(5, FALLEN_X, world_height(FALLEN_X, FALLEN_Z) + 0.45f, FALLEN_Z, FALLEN_FACING, 1.0f);
	}

	/* --- Pell's Cabin -------------------------------------------------------- */
	{
		const float CX = CABIN_X, CZ = CABIN_Z, F = world_cabin_floor();
		add_note(NOTE_FLOWERS, CX + 1.25f, F + 0.84f, CZ - 1.25f);
		add_note(NOTE_LETTER, CX + 1.75f, F + 0.62f, CZ - 0.66f);
		add(IA_CHIMES, 0, CX + CABIN_HALF_X - 0.35f, F + 2.0f, CZ + CABIN_HALF_Z + CABIN_PORCH_D - 0.3f, 0.25f, "Ring");
		add_seat(6, CX - 1.2f, F - 0.03f, CZ + CABIN_HALF_Z + CABIN_PORCH_D - 0.15f, CABIN_FACING, 0.5f);
	}

	/* --- Trailhead ---------------------------------------------------------- */
	add(IA_SUPPLY, 0, SUPPLY_X, world_height(SUPPLY_X, SUPPLY_Z) + 0.85f, SUPPLY_Z, 0.5f, "Open");
	add(IA_GATE, 0, GATE_X, world_height(GATE_X, GATE_Z - 0.3f) + 1.0f, GATE_Z, 0.8f, "Open");
}

const interactable_t *world_interactables(int *count)
{
	*count = s_n;
	return s_ia;
}

int world_find_interactable(ia_kind kind, int id)
{
	for (int i = 0; i < s_n; i++)
		if (s_ia[i].kind == kind && s_ia[i].id == id) return i;
	return -1;
}
