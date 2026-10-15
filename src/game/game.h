/*
 * game.h -- the player, interactions and the play modes.
 */
#ifndef LL_GAME_H
#define LL_GAME_H

#include <stdbool.h>
#include "core/mathx.h"
#include "world/world.h"
#include "game/daycycle.h"

typedef enum { MODE_WALK, MODE_SIT, MODE_READ } play_mode;

typedef struct {
	v3 pos;                  /* feet */
	float vy;
	v3 vel;                  /* horizontal velocity */
	float yaw, pitch, roll;
	float yaw_rate, pitch_rate;
	float snap_left;         /* remaining snap-turn angle */
	float bob_phase, bob;
	surface_t surface;
	play_mode mode;
	int sit_id;
	float sit_blend;         /* 0 standing .. 1 seated camera */
	v3 stand_pos;
	float reticle_x, reticle_y;
	int hover;               /* interactable index or -1 */
	int reading;             /* note id or -1 */
	float read_fade;
	float timelapse;         /* 0..1 visual cue while time is passing */
	float day_card;          /* seconds since the day title card started */
	bool input_locked;       /* screenshot tour drives the camera */
} player_t;

extern player_t P;

/* Options (Phase 5 adds the menu; defaults per DESIGN.md section 5). */
typedef struct {
	float look_sens;         /* 1.0 = 70 deg/s yaw, 45 deg/s pitch at the screen edge */
	bool head_sway;
	bool walk_bob;
	bool snap_turn;
} options_t;

extern options_t OPT;

void game_init(void);
void game_update(float dt);
v3 game_eye(void);
float game_fov(void);

/* Notes and their text (Phase 4 moves these into data files). */
const char *note_text(int id);

#endif
