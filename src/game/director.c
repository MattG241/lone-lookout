/*
 * director.c -- decides when a piece of music starts (see director.h).
 */
#include <string.h>
#include "game/director.h"
#include "game/game.h"
#include "audio/audio.h"

#define QUIET_BEFORE 240.0f      /* seconds without music before it may come back on its own */
#define SIT_FOR 20.0f

static struct {
	float quiet, sit_t, last_hour;
	bool sit_cued, opened;
	float music_t, total_t;
} D;

void director_init(void)
{
	memset(&D, 0, sizeof(D));
	D.quiet = QUIET_BEFORE;
	D.last_hour = -1.0f;
}

void director_skip_opening(void) { D.opened = true; }

float director_music_share(void) { return D.total_t > 0.0f ? D.music_t / D.total_t : 0.0f; }

void director_story_cue(const char *piece)
{
	audio_music_cue(piece);
	D.quiet = 0.0f;
}

static const char *morning_piece(void)
{
	if (S.day >= NUM_DAYS) return "rain";
	return S.day <= 4 ? "settling" : "channel";
}

/* An area's piece, the first time you arrive each day. */
static const char *area_piece(int area)
{
	if (area == AREA_TARN) return "tarn";
	if (area == AREA_BURN) return "ash";
	if (S.day >= NUM_DAYS) return "rain";
	if (DAY.hour < 11.0f) return morning_piece();
	return DAY.hour > 18.0f ? "seven" : "channel";
}

static const char *seat_piece(int seat)
{
	if (seat == 3) return "tarn";                 /* the dock */
	if (seat == 5) return "ash";                  /* the fallen snag */
	if (S.day >= NUM_DAYS) return "rain";
	if (DAY.hour > 17.5f) return "seven";
	return DAY.hour < 11.0f ? morning_piece() : "channel";
}

static void maybe(const char *piece)
{
	if (D.quiet < QUIET_BEFORE || audio_music_playing()) return;
	audio_music_cue(piece);
	D.quiet = 0.0f;
}

void director_update(float dt)
{
	if (P.input_locked) return;
	bool playing = audio_music_playing();
#ifdef LL_SOAK
	/* The soak plays the pieces back to back, the worst case for the mixer. */
	static int next;
	static float wait;
	static const char *ALL[] = { "channel", "settling", "tarn", "ash", "seven", "rain", "channel_end" };
	if (!playing && (wait -= dt) <= 0.0f) {
		audio_music_cue(ALL[next++ % 7]);
		wait = 3.0f;                       /* the mixer takes a moment to report it playing */
	}
#endif
	D.total_t += dt;
	if (playing) D.music_t += dt;
	else D.quiet += dt;

	bool awake = P.mode != MODE_SLEEP && P.mode != MODE_ENDING && S.fade < 0.3f && P.day_card > 4.0f;
	float h = DAY.hour, last = D.last_hour;
	D.last_hour = h;
	if (!awake) return;

	/* Day 1: the main theme as Theo sets off up the trail (not when continuing). */
	if (!D.opened) {
		D.opened = true;
		if (S.day == 1 && h < 16.5f && !playing) director_story_cue("channel");
		return;
	}
	bool outdoors = world_indoor(P.pos) == INDOOR_NONE;
	if (last >= 0.0f && outdoors) {
		if (last < 6.25f && h >= 6.25f) maybe(morning_piece());                    /* sunrise */
		if (last < 18.6f && h >= 18.6f) maybe(S.day >= NUM_DAYS ? "rain" : "seven");   /* golden hour */
	}
	if (S.area_card >= 0 && S.area_card_t < dt * 1.5f) maybe(area_piece(S.area_card));

	if (P.mode == MODE_SIT && P.sit_blend > 0.9f) {
		D.sit_t += dt;
		if (D.sit_t > SIT_FOR && !D.sit_cued) {
			int n;
			const interactable_t *ia = world_interactables(&n);
			D.sit_cued = true;
			maybe(seat_piece(ia[P.sit_id].id));
		}
	} else {
		D.sit_t = 0.0f;
		D.sit_cued = false;
	}
}
