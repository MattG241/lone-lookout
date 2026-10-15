/*
 * tour.c -- scripted screenshot tour (built with `make SHOTS=1`).
 *
 * Plays a fixed list of camera poses and game states, one after another,
 * each preceded by a few pure-black frames so captured frame dumps can be
 * split into shots automatically. The normal build compiles this to nothing.
 */
#include <math.h>
#include "game/tour.h"
#include "game/game.h"
#include "game/radio.h"
#include "world/world.h"

#ifdef LL_SHOT_TOUR

#define BLACK_FRAMES 12
#define HOLD_FRAMES 110

typedef enum { SHOT_PLAIN, SHOT_CHOICES, SHOT_SUBTITLE, SHOT_READ, SHOT_SIT_LAPSE } shot_kind;

typedef struct {
	const char *name;
	float hour;
	float x, z, eye_up;      /* eye_up: metres above the ground at (x, z) */
	float look_x, look_y_up, look_z;
	shot_kind kind;
} shot_t;

static const shot_t SHOTS[] = {
	{ "noon_test_area", 13.0f, 15.0f, 31.0f, 1.62f, 0.0f, 6.0f, -10.0f, SHOT_PLAIN },
	{ "golden_from_deck", 18.55f, -2.3f, -7.7f, 9.0f + 1.62f, -90.0f, 4.0f, 22.0f, SHOT_PLAIN },
	{ "golden_tower_silhouette", 18.75f, 26.0f, -2.0f, 1.62f, 0.0f, 7.0f, -10.0f, SHOT_SUBTITLE },
	{ "radio_choices", 18.3f, 5.0f, 24.0f, 1.62f, 0.0f, 6.0f, -10.0f, SHOT_CHOICES },
	{ "read_prompt", 10.0f, 1.55f, 5.9f, 1.55f, 0.64f, 1.2f, 4.56f, SHOT_PLAIN },
	{ "note_open", 10.0f, 1.55f, 5.9f, 1.55f, 0.64f, 1.2f, 4.56f, SHOT_READ },
	{ "sit_timelapse", 18.9f, -14.5f, 8.0f, 0.0f, -40.0f, 1.0f, 8.0f, SHOT_SIT_LAPSE },
	{ "night", 22.8f, 5.0f, 24.0f, 1.62f, 0.0f, 9.0f, -10.0f, SHOT_PLAIN },
};
#define NUM_SHOTS ((int)(sizeof(SHOTS) / sizeof(SHOTS[0])))

static int s_shot = -1;
static int s_frame;

static void begin_shot(const shot_t *s)
{
	P.input_locked = true;
	P.day_card = 100.0f;
	P.mode = MODE_WALK;
	P.sit_blend = 0.0f;
	P.timelapse = 0.0f;
	P.reading = -1;
	P.read_fade = 0.0f;
	DAY.hour = s->hour;

	/* eye_up > 5 means "standing on the deck": take the deck as the ground. */
	bool deck = s->eye_up > 5.0f;
	float g = deck ? world_pad_height() + TOWER_DECK_Y : world_ground(s->x, s->z, 1e9f, NULL);
	float eye_up = deck ? 1.62f : s->eye_up;
	P.pos = v3_make(s->x, g + eye_up - 1.62f, s->z);
	P.bob = 0.0f;

	float ly = world_height(s->look_x, s->look_z) + s->look_y_up;
	float dx = s->look_x - s->x, dz = s->look_z - s->z, dy = ly - (g + eye_up);
	P.yaw = atan2f(dx, -dz);
	P.pitch = atan2f(dy, sqrtf(dx * dx + dz * dz));
	P.reticle_x = P.reticle_y = 0.0f;

	switch (s->kind) {
	case SHOT_CHOICES:
		radio_debug_choices(CALL_D1_RADIO_CHECK, 0, 0);
		break;
	case SHOT_SUBTITLE:
		radio_debug_line(CALL_D1_RADIO_CHECK, 5, 1, 0.5f);
		break;
	case SHOT_READ:
		P.mode = MODE_READ;
		P.reading = 0;
		break;
	case SHOT_SIT_LAPSE: {
		int n;
		const interactable_t *ia = world_interactables(&n);
		P.mode = MODE_SIT;
		P.sit_id = 1;
		P.sit_blend = 1.0f;
		P.timelapse = 1.0f;
		P.yaw = ia[1].sit_yaw + DEG2RAD(12.0f);
		P.pitch = DEG2RAD(2.0f);
		break;
	}
	default:
		radio_init();
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
	if (SHOTS[s_shot].kind == SHOT_SUBTITLE && s_frame == BLACK_FRAMES)
		radio_debug_line(CALL_D1_RADIO_CHECK, 5, 1, 0.5f);
	s_frame++;
	return s_frame <= BLACK_FRAMES;
}

#else

bool tour_frame(void) { return false; }

#endif
