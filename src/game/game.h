/*
 * game.h -- the player, the play modes, and the state of the summer.
 */
#ifndef LL_GAME_H
#define LL_GAME_H

#include <stdbool.h>
#include "core/mathx.h"
#include "world/world.h"
#include "game/daycycle.h"
#include "game/days.h"
#include "audio/ambient.h"

typedef enum {
	MODE_WALK,
	MODE_SIT,
	MODE_READ,          /* a note, the logbook or the supply box card */
	MODE_FINDER,        /* sighting through the fire finder */
	MODE_NOTEBOOK,
	MODE_SLEEP,         /* fading through the night to the next morning */
	MODE_ENDING,        /* through the gate on the last evening: the closing card and credits */
} play_mode;

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
	int sit_id;              /* interactable index while seated */
	float sit_blend;         /* 0 standing .. 1 seated camera */
	v3 stand_pos;
	float reticle_x, reticle_y;
	int hover;               /* interactable index or -1 */
	int reading;             /* text id (see game_text) or -1 */
	float read_fade;
	float timelapse;         /* 0..1 visual cue while time is passing */
	float day_card;          /* seconds since the day title card started */
	bool binoculars_up;
	float zoom;              /* 0 naked eye .. 1 binoculars */
	float finder_fade;
	float notebook_fade;
	bool input_locked;       /* screenshot tour drives the camera */
} player_t;

extern player_t P;

/* What has happened this summer. */
typedef struct {
	int day;                         /* 1..NUM_DAYS */
	bool found[NOTE_COUNT];          /* notes read */
	bool binoculars;                 /* taken from the hook */
	bool lamp_on;
	float lamp;                      /* smoothed 0..1 */
	float kettle_t;                  /* < 0 idle, else seconds since it went on */
	bool kettle_today;
	bool supply_opened[NUM_DAYS + 1];
	bool obs_am, obs_pm;             /* today's weather readings taken (morning, evening) */
	u32 areas_today;                 /* area name cards shown today */
	int area;                        /* area the player is in, or -1 */
	float area_card_t;
	int area_card;
	const char *thought;             /* a line Theo thinks (no radio) */
	float thought_t;
	u32 thoughts_said;               /* one-time thoughts */
	const char *hint;                /* a small control hint */
	float hint_t;
	int last_tread;
	float sleep_t;                   /* MODE_SLEEP timer */
	float fade;                      /* 0..1 to black */
	bool season_dirty;               /* main loop: rebuild the season's colours */
	int notebook_page, notebook_sel, sound_sel;
	u8 sound_day[SND_COUNT];         /* the sound log: the day each was written down, 0 if not yet */
	float ending_t;                  /* MODE_ENDING timer */
	bool ended;                      /* the credits have rolled once */
} story_t;

extern story_t S;

/* Options (the Options screen; DESIGN.md sections 5 and 11). Saved with the game. */
typedef struct {
	float look_sens;         /* 1.0 = 70 deg/s yaw, 45 deg/s pitch at the screen edge */
	u8 comfort;              /* comfort box: 0 small, 1 medium, 2 large */
	bool invert_pitch;
	bool snap_turn;
	bool head_sway;
	bool walk_bob;
	bool compass;            /* a pencilled heading at the top of the screen */
	bool subs_large;
	bool subs_backing;       /* a dark band behind subtitles */
	bool hearing;            /* Theo's left ear, while listening */
	bool mono;
	u8 vol_music, vol_ambience, vol_voice;   /* 0..10 */
	bool lock30;             /* always 30 fps (25 on PAL) */
} options_t;

extern options_t OPT;
void options_apply(void);                /* push the audio options to the mixer (game/save.c) */

void game_init(void);
void game_update(float dt);
v3 game_eye(void);
v3 game_reticle_dir(void);               /* world-space ray through the reticle */
float game_fov(void);
bool game_radio_allowed(void);           /* B may raise the radio now */
void game_think(const char *text);       /* show one of Theo's thoughts */
void game_new_day(int day);              /* jump to a morning (sleep, tour) */
float game_season(void);

/* The ending: seconds into MODE_ENDING when the closing card gives way to the credits. */
#define ENDING_CREDITS_AT 14.0f

/* Readable texts: notes (0..NOTE_COUNT-1), the logbook and supply box cards. */
#define TEXT_LOGBOOK 100
#define TEXT_SUPPLY 200
#define TEXT_WEATHER 300
const char *game_text(int id);
const char *note_title(int id);

#endif
