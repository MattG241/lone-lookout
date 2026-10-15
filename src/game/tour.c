/*
 * tour.c -- scripted screenshot tour (built with `make SHOTS=1`).
 *
 * Plays a fixed list of camera poses and game states, one after another,
 * each preceded by a few pure-black frames so captured frame dumps can be
 * split into shots automatically (tools/capture/split_shots.py). The normal
 * build compiles this to nothing.
 */
#include <math.h>
#include <string.h>
#include "game/tour.h"
#include "game/game.h"
#include "game/radio.h"
#include "game/notebook.h"
#include "world/world.h"
#include "world/sites.h"

#ifdef LL_SHOT_TOUR

#define BLACK_FRAMES 12
#define HOLD_FRAMES 110

typedef enum {
	SHOT_PLAIN, SHOT_DAY_CARD, SHOT_CHOICES, SHOT_SEAT, SHOT_LAMP, SHOT_NOTEBOOK, SHOT_BINOCULARS, SHOT_FINDER,
	SHOT_READ,
} shot_kind;

typedef struct {
	const char *name;
	int day;
	float hour;
	float x, z, eye_up;          /* eye_up: metres above the highest ground at (x, z) */
	float lx, ly_up, lz;         /* look at: a point ly_up above the highest ground there */
	shot_kind kind;
	int arg;                     /* seat id, note id, notebook page */
} shot_t;

#define TX TOWER_X
#define TZ TOWER_Z

static const shot_t SHOTS[] = {
	{ "trailhead_day1", 1, 15.55f, START_X, START_Z, 1.62f, 252.0f, 6.0f, 440.0f, SHOT_DAY_CARD, 0 },
	{ "tower_from_trail", 1, 16.2f, 263.0f, 296.0f, 1.62f, TX, 9.0f, TZ, SHOT_PLAIN, 0 },
	{ "meadow_afternoon", 1, 17.2f, 156.0f, 318.0f, 1.62f, 96.0f, 3.0f, 344.0f, SHOT_PLAIN, 0 },
	{ "ridge_vista", 1, 18.65f, 0, 0, 0, TX, 8.0f, TZ, SHOT_SEAT, 4 },
	{ "from_the_deck", 1, 18.4f, TX - 2.5f, TZ - 2.0f, 1.62f, 112.0f, 40.0f, 334.0f, SHOT_PLAIN, 0 },
	{ "cab_night", 1, 22.9f, TX + 0.3f, TZ + 1.55f, 1.62f, TX - 1.3f, 0.7f, TZ - 0.9f, SHOT_LAMP, 0 },
	{ "tower_at_night", 1, 23.1f, 263.0f, 296.0f, 1.62f, TX, 10.0f, TZ, SHOT_LAMP, 0 },
	{ "tarn_dawn", 2, 6.4f, 0, 0, 0, 104.0f, 4.0f, 200.0f, SHOT_SEAT, 3 },
	{ "burn_morning", 2, 8.6f, 372.0f, 394.0f, 1.62f, 404.0f, 3.0f, 358.0f, SHOT_PLAIN, 0 },
	{ "cabin_afternoon", 3, 15.2f, 254.0f, 447.0f, 1.62f, 243.0f, 1.6f, 434.0f, SHOT_PLAIN, 0 },
	{ "notebook_map", 3, 16.0f, TX + 0.3f, TZ + 1.2f, 1.62f, TX, 1.0f, TZ - 2.0f, SHOT_NOTEBOOK, NB_MAP },
	{ "binoculars", 1, 17.6f, TX + 2.5f, TZ - 2.5f, 1.62f, 398.0f, 2.0f, 112.0f, SHOT_BINOCULARS, 0 },
	{ "fire_finder", 4, 15.4f, TX + 0.2f, TZ + 0.85f, 1.62f, TX, 1.0f, TZ - 3.0f, SHOT_FINDER, 0 },
	{ "storm_day4", 4, 15.8f, TX + 2.5f, TZ + 2.5f, 1.62f, 400.0f, 20.0f, 330.0f, SHOT_PLAIN, 0 },
	{ "haze_day5", 5, 18.9f, 0, 0, 0, TX, 8.0f, TZ, SHOT_SEAT, 4 },
	{ "rain_day7", 7, 14.0f, 0, 0, 0, 250.0f, 1.5f, 470.0f, SHOT_SEAT, 6 },
	{ "radio_choices", 1, 17.05f, TX - 0.2f, TZ + 1.2f, 1.62f, TX - 1.8f, 1.2f, TZ - 1.8f, SHOT_CHOICES, 0 },
	{ "note_bench", 5, 18.7f, 0, 0, 0, TX, 8.0f, TZ, SHOT_READ, NOTE_BENCH },
};
#define NUM_SHOTS ((int)(sizeof(SHOTS) / sizeof(SHOTS[0])))

static int s_shot = -1;
static int s_frame;

static float top_ground(float x, float z) { return world_ground(x, z, 1e9f, NULL); }

static void look_at(v3 eye, float lx, float ly, float lz)
{
	float dx = lx - eye.x, dz = lz - eye.z, dy = ly - eye.y;
	P.yaw = atan2f(dx, -dz);
	P.pitch = atan2f(dy, sqrtf(dx * dx + dz * dz));
}

static void begin_shot(const shot_t *s)
{
	if (S.day != s->day) {
		S.day = s->day;
		S.season_dirty = true;
	}
	P.input_locked = true;
	P.day_card = s->kind == SHOT_DAY_CARD ? 2.6f : 100.0f;
	P.mode = MODE_WALK;
	P.sit_blend = 0.0f;
	P.timelapse = 0.0f;
	P.reading = -1;
	P.read_fade = 0.0f;
	P.binoculars_up = false;
	P.zoom = 0.0f;
	P.finder_fade = 0.0f;
	P.notebook_fade = 0.0f;
	P.bob = 0.0f;
	P.reticle_x = P.reticle_y = 0.0f;
	S.lamp_on = s->kind == SHOT_LAMP;
	S.lamp = S.lamp_on ? 1.0f : 0.0f;
	S.fade = 0.0f;
	S.thought = 0;
	S.hint = 0;
	S.area_card_t = 100.0f;
	S.binoculars = s->kind == SHOT_BINOCULARS;
	DAY.hour = s->hour;
	weather_update(S.day, DAY.hour, 100.0f);         /* settle the weather at once */
	radio_init();

	int n;
	const interactable_t *ia = world_interactables(&n);
	if (s->kind == SHOT_SEAT || s->kind == SHOT_READ) {
		int seat = world_find_interactable(IA_SIT, s->kind == SHOT_READ ? 4 : s->arg);
		P.mode = MODE_SIT;
		P.sit_id = seat;
		P.sit_blend = 1.0f;
		P.pos = v3_make(ia[seat].sit_eye.x, top_ground(ia[seat].sit_eye.x, ia[seat].sit_eye.z), ia[seat].sit_eye.z);
		look_at(ia[seat].sit_eye, s->lx, top_ground(s->lx, s->lz) + s->ly_up, s->lz);
		if (s->kind == SHOT_READ) {
			P.mode = MODE_READ;
			P.reading = s->arg;
			P.read_fade = 1.0f;
		}
		return;
	}
	P.pos = v3_make(s->x, top_ground(s->x, s->z) + s->eye_up - 1.62f, s->z);
	look_at(v3_add(P.pos, v3_make(0, 1.62f, 0)), s->lx, top_ground(s->lx, s->lz) + s->ly_up, s->lz);

	switch (s->kind) {
	case SHOT_CHOICES:
		radio_debug_choices(CALL_D1_RADIO_CHECK, 0, 0);
		break;
	case SHOT_NOTEBOOK:
		S.found[NOTE_MAP] = true;
		S.notebook_page = s->arg;
		P.mode = MODE_NOTEBOOK;
		P.notebook_fade = 1.0f;
		break;
	case SHOT_BINOCULARS:
		P.binoculars_up = true;
		P.zoom = 1.0f;
		break;
	case SHOT_FINDER:
		P.mode = MODE_FINDER;
		P.finder_fade = 1.0f;
		P.pitch = DEG2RAD(-8.0f);
		break;
	default:
		break;
	}
}

bool tour_frame(void)
{
	if (s_shot < 0 || s_frame >= BLACK_FRAMES + HOLD_FRAMES) {
		s_shot = (s_shot + 1) % NUM_SHOTS;
		s_frame = 0;
		begin_shot(&SHOTS[s_shot]);
	}
	s_frame++;
	return s_frame <= BLACK_FRAMES;
}

#else

bool tour_frame(void) { return false; }

#endif
