/*
 * story.c -- story flags, errands, the Day 5 smoke, the Day 8 elk, the doe
 * and fawns, and the dialogue hooks.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game/story.h"
#include "game/game.h"
#include "game/radio.h"
#include "game/listen.h"
#include "game/director.h"
#include "gfx/gfx.h"
#include "audio/audio.h"
#include "world/sites.h"

#define SMOKE_BEARING 104.0f
#define SMOKE_DIST 900.0f
#define SMOKE_FROM 16.5f

static struct {
	const char *errand;
	bool errand_done;
	bool season_over;
	readings_t am, pm;
	float smoke_seen_t;
	float sighted_bearing;
	float elk_t;                 /* < 0 not started */
	int elk_next;
	float deer_fled;             /* > 0: fleeing, seconds */
	bool deer_gone;
	float fawn_seen_t;
	float bugle_wait;            /* Days 9-11 at dusk: the odd far bugle, unscripted */
} T;

/* The bulls: where each bugle comes from, and when (seconds after "listen"). */
static const float ELK[4][3] = {
	{ 8.0f, 70.0f, 290.0f }, { 27.0f, 150.0f, 368.0f }, { 49.0f, 60.0f, 372.0f }, { 74.0f, 128.0f, 352.0f },
};

/* Where the doe and fawns graze, below the drift fence. */
static const float DEER_HOME[DEER_COUNT][3] = {
	{ 132.0f, 296.0f, 0.9f }, { 128.5f, 299.0f, 0.55f }, { 135.0f, 300.5f, 0.55f },
};

void story_init(void)
{
	memset(&T, 0, sizeof(T));
	T.elk_t = -1.0f;
}

void story_new_day(int day)
{
	(void)day;
	dlg_set_flag(FLAG_OBS_AM, false);
	dlg_set_flag(FLAG_OBS_PM, false);
	dlg_set_flag(FLAG_AT_SPRING, false);
	dlg_set_flag(FLAG_SUPPLY_TODAY, false);
	T.errand = 0;
	T.errand_done = false;
	T.deer_fled = 0.0f;
	T.deer_gone = false;
	T.fawn_seen_t = 0.0f;
	T.bugle_wait = 20.0f;
}

void story_raise(int flag) { dlg_set_flag(flag, true); }
bool story_flag(int flag) { return dlg_flag(flag); }

void story_note_read(int note)
{
	if (note >= 0 && note < NOTE_COUNT) story_raise(FLAG_NOTE_WELCOME + note);
}

void story_readings_taken(bool morning)
{
	readings_t r = weather_read(S.day, DAY.hour);
	if (morning) {
		T.am = r;
		story_raise(FLAG_OBS_AM);
	} else {
		T.pm = r;
		story_raise(FLAG_OBS_PM);
	}
}

const char *story_errand(void) { return T.errand; }
bool story_errand_done(void) { return T.errand_done; }

bool story_can_leave(void)
{
	return S.day >= NUM_DAYS && (T.season_over || DAY.hour >= 20.5f || DAY.hour < 5.0f);
}

dlg_world story_world(void)
{
	dlg_world w;
	w.day = S.day;
	w.hour = DAY.hour;
	w.area = world_area_at(P.pos.x, P.pos.z);
	w.indoor = (int)world_indoor(P.pos);
	w.seat = -1;
	if (P.mode == MODE_SIT || (P.sit_blend > 0.5f && P.mode != MODE_WALK)) {
		int n;
		const interactable_t *ia = world_interactables(&n);
		if (P.sit_id >= 0 && P.sit_id < n) w.seat = ia[P.sit_id].id;
	}
	w.can_ring = P.mode != MODE_SLEEP && P.mode != MODE_ENDING && !P.input_locked && P.day_card > 5.0f && S.fade < 0.3f;
	return w;
}

/* ------------------------------------------------------------------------ */
/* Day 5: smoke on Harrow Ridge                                             */
/* ------------------------------------------------------------------------ */

bool story_smoke(v3 *base, float *height, float *alpha)
{
	if (S.day != 5 || DAY.hour < SMOKE_FROM || DAY.hour > 22.0f) return false;
	float b = DEG2RAD(SMOKE_BEARING);
	/* The base is behind the eastern rim (14 degrees up from the deck); the column
	 * climbs well clear of it, to about 27 degrees. */
	*base = v3_make(TOWER_X + sinf(b) * SMOKE_DIST, 120.0f, TOWER_Z - cosf(b) * SMOKE_DIST);
	float age = DAY.hour - SMOKE_FROM;
	*height = 120.0f + 300.0f * saturatef(age / 0.6f);
	/* The crew has it by dark: it thins after a report (or Prospect's). */
	float fade = dlg_flag(FLAG_SMOKE_REPORTED) || dlg_flag(FLAG_SMOKE_MISSED) ? 1.0f - saturatef((DAY.hour - 19.0f) / 2.0f) : 1.0f;
	*alpha = saturatef(age / 0.15f) * fade;
	return *alpha > 0.01f;
}

float story_smoke_bearing(void) { return T.sighted_bearing; }

void story_finder_sight(float yaw)
{
	float deg = RAD2DEG(yaw);
	while (deg < 0.0f) deg += 360.0f;
	while (deg >= 360.0f) deg -= 360.0f;
	v3 b;
	float h, a;
	if (story_smoke(&b, &h, &a) && fabsf(deg - SMOKE_BEARING) < 6.0f) {
		T.sighted_bearing = deg;
		story_raise(FLAG_SMOKE_SIGHTED);
		story_raise(FLAG_SMOKE_SEEN);
		game_think("Bearing one-oh-four. Call it in.");
	} else {
		char buf[48];
		snprintf(buf, sizeof(buf), "Bearing %03d. Nothing there.", (int)(deg + 0.5f) % 360);
		static char keep[48];
		strcpy(keep, buf);
		game_think(keep);
	}
}

static void watch_smoke(float dt)
{
	v3 b;
	float h, a;
	if (!story_smoke(&b, &h, &a) || dlg_flag(FLAG_SMOKE_SEEN)) return;
	v3 top = v3_add(b, v3_make(0, h * 0.6f, 0));
	float sx, sy;
	bool on = gfx_project(top, &sx, &sy) && sx > 0 && sx < G.ui_w && sy > 0 && sy < G.ui_h;
	T.smoke_seen_t = on ? T.smoke_seen_t + dt : 0.0f;
	if (T.smoke_seen_t > 1.5f) {
		story_raise(FLAG_SMOKE_SEEN);
		game_think("Smoke. A thin white column, out past the burn.");
	}
}

/* ------------------------------------------------------------------------ */
/* Day 8: the bulls                                                         */
/* ------------------------------------------------------------------------ */

static void play_at(ui_sound_t s, float x, float z, float gain)
{
	if (s == UI_ELK) listen_event(SND_ELK, v3_make(x, world_height(x, z) + 2.0f, z));
	v3 eye = game_eye();
	float dx = x - eye.x, dz = z - eye.z, d = sqrtf(dx * dx + dz * dz) + 1.0f;
	float pan = clampf((dx * cosf(P.yaw) + dz * sinf(P.yaw)) / d, -1.0f, 1.0f) * 0.8f;
	audio_ui_pan(s, gain * clampf(90.0f / d, 0.25f, 1.0f), pan);
}

static void bulls(float dt)
{
	/* After Day 8 the bulls are still about at dusk, now and then. */
	if (T.elk_t < 0.0f && S.day >= 9 && DAY.hour > 18.5f && DAY.hour < 20.5f) {
		if ((T.bugle_wait -= dt) <= 0.0f) {
			int k = (int)(DAY.hour * 977.0f) & 3;
			play_at(UI_ELK, ELK[k][1], ELK[k][2], 0.8f);
			T.bugle_wait = 70.0f + (float)(((int)(DAY.hour * 7919.0f)) % 90);
		}
	}
	if (T.elk_t < 0.0f) return;
	T.elk_t += dt;
	while (T.elk_next < 4 && T.elk_t >= ELK[T.elk_next][0]) {
		play_at(UI_ELK, ELK[T.elk_next][1], ELK[T.elk_next][2], 1.0f);
		T.elk_next++;
	}
	if (T.elk_next >= 4 && T.elk_t > ELK[3][0] + 8.0f && !dlg_flag(FLAG_ELK_DONE)) {
		story_raise(FLAG_ELK_DONE);
		game_think("Four. I think four. Call it in.");
	}
}

/* ------------------------------------------------------------------------ */
/* The doe and fawns                                                        */
/* ------------------------------------------------------------------------ */

static bool deer_today(void)
{
	return S.day >= 2 && S.day <= 9 && DAY.hour > 5.5f && DAY.hour < 10.0f && !T.deer_gone;
}

bool story_deer(int i, v3 *pos, float *facing, float *alpha, float *scale)
{
	if (!deer_today()) return false;
	float x = DEER_HOME[i][0], z = DEER_HOME[i][1];
	/* A slow graze: each one drifts a metre or two through the morning. */
	float t = DAY.hour * 0.7f + i * 2.1f;
	x += sinf(t) * 1.5f;
	z += cosf(t * 0.8f) * 1.0f;
	float away = 0.0f;
	if (T.deer_fled > 0.0f) {
		/* Bounding off north-west, fading into the trees. */
		away = T.deer_fled * 6.0f;
		x -= away * 0.7f;
		z -= away * 0.7f;
	}
	*pos = v3_make(x, world_height(x, z), z);
	*facing = T.deer_fled > 0.0f ? BEARING(315.0f) : BEARING(60.0f + 50.0f * i + 20.0f * sinf(t * 0.5f));
	*alpha = 1.0f - saturatef(T.deer_fled / 3.0f);
	*scale = DEER_HOME[i][2];
	return true;
}

static void watch_deer(float dt)
{
	if (!deer_today()) return;
	v3 eye = game_eye();
	float dx = DEER_HOME[0][0] - eye.x, dz = DEER_HOME[0][1] - eye.z;
	float d = sqrtf(dx * dx + dz * dz);
	if (T.deer_fled > 0.0f) {
		T.deer_fled += dt;
		if (T.deer_fled > 3.5f) T.deer_gone = true;
		return;
	}
	if (d < 22.0f && !P.input_locked) {
		T.deer_fled = 0.01f;           /* too close: off they go */
		return;
	}
	float sx, sy;
	v3 c = v3_make(DEER_HOME[0][0], world_height(DEER_HOME[0][0], DEER_HOME[0][1]) + 0.8f, DEER_HOME[0][1]);
	bool on = d < 110.0f && gfx_project(c, &sx, &sy) && sx > 60 && sx < G.ui_w - 60 && sy > 40 && sy < G.ui_h - 40;
	T.fawn_seen_t = on ? T.fawn_seen_t + dt : 0.0f;
	if (T.fawn_seen_t > 1.5f && !dlg_flag(FLAG_FAWNS_SEEN)) story_raise(FLAG_FAWNS_SEEN);
}

/* ------------------------------------------------------------------------ */

void story_update(float dt)
{
	if (P.input_locked) return;
	/* Places. */
	float dx = P.pos.x - SPRING_X, dz = P.pos.z - SPRING_Z;
	if (dx * dx + dz * dz < 5.0f * 5.0f) story_raise(FLAG_AT_SPRING);
	if (world_indoor(P.pos) == INDOOR_CABIN) story_raise(FLAG_CABIN_ENTERED);
	if (S.day == NUM_DAYS && WX.rain > 0.3f) story_raise(FLAG_RAIN_ON);
	watch_smoke(dt);
	watch_deer(dt);
	bulls(dt);
}

/* ------------------------------------------------------------------------ */
/* Dialogue hooks                                                           */
/* ------------------------------------------------------------------------ */

void dlg_hook_action(int action, const char *arg)
{
	switch (action) {
	case ACT_ERRAND:
		T.errand = arg;
		T.errand_done = false;
		break;
	case ACT_ERRAND_DONE:
		T.errand_done = true;
		break;
	case ACT_ELK:
		T.elk_t = 0.0f;
		T.elk_next = 0;
		break;
	case ACT_HISS_HOLD:
		radio_hold_hiss(60.0f);
		break;
	case ACT_MUSIC:
		director_story_cue(arg);
		break;
	case ACT_SEASON_OVER:
		T.season_over = true;
		break;
	default:
		break;
	}
}

void dlg_hook_token(const char *name, char *out, int n)
{
	bool evening = DAY.hour >= 13.0f;
	readings_t r = evening ? (dlg_flag(FLAG_OBS_PM) ? T.pm : weather_read(S.day, DAY.hour))
	                       : (dlg_flag(FLAG_OBS_AM) ? T.am : weather_read(S.day, DAY.hour));
	if (!strcmp(name, "dry")) snprintf(out, n, "%d", r.dry);
	else if (!strcmp(name, "wet")) snprintf(out, n, "%d", r.wet);
	else if (!strcmp(name, "rh")) snprintf(out, n, "%d", r.rh);
	else if (!strcmp(name, "wind")) snprintf(out, n, "%s at %d", r.wind_dir, r.wind_mph);
	else if (!strcmp(name, "fuel")) snprintf(out, n, "%.1f", r.fuel);
	else if (!strcmp(name, "bearing")) snprintf(out, n, "%03d", (int)(T.sighted_bearing + 0.5f) % 360);
	else snprintf(out, n, "?");
}

void dlg_hook_call_end(int call)
{
	/* After the smoke night, the theme. */
	if (call == CALL_D5_NIGHT) director_story_cue("channel");
}
