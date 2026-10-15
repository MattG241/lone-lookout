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
#include "game/story.h"
#include "game/listen.h"
#include "world/world.h"
#include "world/sites.h"

#ifdef LL_SHOT_TOUR

#define BLACK_FRAMES 12
#define HOLD_FRAMES 110

typedef enum {
	SHOT_PLAIN, SHOT_DAY_CARD, SHOT_CHOICES, SHOT_SEAT, SHOT_LAMP, SHOT_NOTEBOOK, SHOT_BINOCULARS, SHOT_FINDER,
	SHOT_READ, SHOT_ENDING, SHOT_LISTEN,
} shot_kind;

typedef struct {
	const char *name;
	int day;
	float hour;
	float x, z, eye_up;          /* eye_up: metres above the highest ground at (x, z) */
	float lx, ly_up, lz;         /* look at: a point ly_up above the highest ground there */
	shot_kind kind;
	int arg;                     /* seat id, note id, notebook page */
	int call1, lines;            /* a call to show (call id + 1), so many lines in */
	float bearing, pitch;        /* if bearing != 0: face this way instead of looking at lx, lz */
	int play1;                   /* a call to play through first (call id + 1): errands, the log */
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
	{ "fawns_day2", 2, 7.2f, 150.0f, 320.0f, 1.62f, 132.0f, 0.7f, 298.0f, SHOT_BINOCULARS, 0 },
	{ "burn_morning", 2, 8.6f, 372.0f, 394.0f, 1.62f, 404.0f, 3.0f, 358.0f, SHOT_PLAIN, 0 },
	{ "cabin_afternoon", 3, 15.2f, 254.0f, 447.0f, 1.62f, 243.0f, 1.6f, 434.0f, SHOT_PLAIN, 0 },
	{ "notebook_today", 4, 10.4f, TX + 0.3f, TZ + 1.2f, 1.62f, TX, 1.0f, TZ - 2.0f, SHOT_NOTEBOOK, NB_TODAY,
	  0, 0, 0, 0, CALL_D4_AM + 1 },
	{ "notebook_radio", 4, 10.5f, TX + 0.3f, TZ + 1.2f, 1.62f, TX, 1.0f, TZ - 2.0f, SHOT_NOTEBOOK, NB_RADIO,
	  0, 0, 0, 0, CALL_D4_AM + 1 },
	{ "notebook_sounds", 4, 10.6f, TX + 0.3f, TZ + 1.2f, 1.62f, TX, 1.0f, TZ - 2.0f, SHOT_NOTEBOOK, NB_SOUNDS },
	{ "listen_burn", 4, 8.4f, 380.0f, 384.0f, 1.62f, TAG_SNAG_X, 4.0f, TAG_SNAG_Z, SHOT_LISTEN, 0 },
	{ "listen_meadow", 2, 12.5f, 120.0f, 330.0f, 1.62f, GAUGE_X, 1.0f, GAUGE_Z - 20.0f, SHOT_LISTEN, 0 },
	{ "notebook_map", 3, 16.0f, TX + 0.3f, TZ + 1.2f, 1.62f, TX, 1.0f, TZ - 2.0f, SHOT_NOTEBOOK, NB_MAP },
	{ "binoculars", 1, 17.6f, TX + 2.5f, TZ - 2.5f, 1.62f, 398.0f, 2.0f, 112.0f, SHOT_BINOCULARS, 0 },
	{ "fire_finder", 5, 15.4f, TX + 0.2f, TZ + 0.85f, 1.62f, TX, 1.0f, TZ - 3.0f, SHOT_FINDER, 0 },
	{ "storm_day5", 5, 15.8f, TX + 2.5f, TZ + 2.5f, 1.62f, 400.0f, 20.0f, 330.0f, SHOT_PLAIN, 0 },
	{ "smoke_day5", 5, 17.3f, TX + 2.6f, TZ + 0.6f, 1.62f, 0, 0, 0, SHOT_PLAIN, 0, 0, 0, 101.0f, 13.0f },
	{ "smoke_finder_day5", 5, 17.4f, TX + 0.2f, TZ + 0.85f, 1.62f, 0, 0, 0, SHOT_FINDER, 0, 0, 0, 104.0f, -8.0f },
	{ "bench_call_day7", 7, 19.05f, 0, 0, 0, TX, 8.0f, TZ, SHOT_SEAT, 4, CALL_D7_SEVEN + 1, 3 },
	{ "rain_day11", 11, 14.0f, 0, 0, 0, 250.0f, 1.5f, 470.0f, SHOT_SEAT, 6 },
	{ "rain_deck_day11", 11, 15.0f, TX - 2.5f, TZ + 2.4f, 1.62f, 0, 0, 0, SHOT_PLAIN, 0, 0, 0, 215.0f, -4.0f },
	{ "radio_choices", 1, 17.05f, TX - 0.2f, TZ + 1.2f, 1.62f, TX - 1.8f, 1.2f, TZ - 1.8f, SHOT_CHOICES, 0,
	  CALL_D1_ARRIVE + 1, 1 },
	{ "note_bench", 7, 18.7f, 0, 0, 0, TX, 8.0f, TZ, SHOT_READ, NOTE_BENCH },
	{ "ending_card", 11, 21.0f, GATE_X, GATE_Z - 3.0f, 1.62f, GATE_X, 1.0f, GATE_Z + 20.0f, SHOT_ENDING, 0 },
};
#define NUM_SHOTS ((int)(sizeof(SHOTS) / sizeof(SHOTS[0])))

static int s_shot = -1;
static int s_frame;

static float top_ground(float x, float z) { return world_ground(x, z, 1e9f, NULL); }

/* Play a call straight through, taking the first card at every choice. */
static void play_through(int call)
{
	bool locked = P.input_locked;
	P.input_locked = false;                  /* so the lines go into the radio log */
	dlg_force_call(call);
	for (int guard = 0; guard < 400 && dlg_get_state() != DS_IDLE; guard++) {
		switch (dlg_get_state()) {
		case DS_LINE: dlg_next(); break;
		case DS_CHOICE: dlg_choose(0); break;
		default: dlg_close(); break;
		}
	}
	P.input_locked = locked;
}

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
	dlg_init();
	story_init();
	story_new_day(S.day);
	if (s->play1) play_through(s->play1 - 1);
	if (s->kind == SHOT_NOTEBOOK) S.obs_am = true;
	listen_debug_force(s->kind == SHOT_LISTEN);
	if (s->kind == SHOT_NOTEBOOK && s->arg == NB_SOUNDS) {
		static const int LOGGED[] = { SND_CREEK, SND_GRASSHOPPERS, SND_SANDPIPER, SND_WOODPECKER, SND_STAIR };
		memset(S.sound_day, 0, sizeof(S.sound_day));
		for (int i = 0; i < 5; i++) S.sound_day[LOGGED[i]] = (u8)(2 + i / 2);
		S.sound_sel = SND_WOODPECKER;
	}

	int n;
	const interactable_t *ia = world_interactables(&n);
	if (s->kind == SHOT_SEAT || s->kind == SHOT_READ) {
		int seat = world_find_interactable(IA_SIT, s->kind == SHOT_READ ? 4 : s->arg);
		P.mode = MODE_SIT;
		P.sit_id = seat;
		P.sit_blend = 1.0f;
		P.pos = v3_make(ia[seat].sit_eye.x, top_ground(ia[seat].sit_eye.x, ia[seat].sit_eye.z), ia[seat].sit_eye.z);
		look_at(ia[seat].sit_eye, s->lx, top_ground(s->lx, s->lz) + s->ly_up, s->lz);
		if (s->call1) radio_debug_call(s->call1 - 1, s->lines);
		if (s->kind == SHOT_READ) {
			P.mode = MODE_READ;
			P.reading = s->arg;
			P.read_fade = 1.0f;
		}
		return;
	}
	P.pos = v3_make(s->x, top_ground(s->x, s->z) + s->eye_up - 1.62f, s->z);
	if (s->bearing != 0.0f) {
		P.yaw = BEARING(s->bearing);
		P.pitch = DEG2RAD(s->pitch);
	} else {
		look_at(v3_add(P.pos, v3_make(0, 1.62f, 0)), s->lx, top_ground(s->lx, s->lz) + s->ly_up, s->lz);
	}
	if (s->call1) radio_debug_call(s->call1 - 1, s->lines);

	switch (s->kind) {
	case SHOT_ENDING:
		P.mode = MODE_ENDING;
		S.ending_t = 8.5f;
		S.fade = 1.0f;
		dlg_set_flag(FLAG_STAIR_IS_EFLAT, true);
		dlg_set_flag(FLAG_PS_TOLD, true);
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
