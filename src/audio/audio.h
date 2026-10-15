/*
 * audio.h -- the game's view of the procedural sound engine.
 *
 * Everything audible is synthesised in a mixer thread (no samples): wind,
 * birds, footsteps, radio static and the "radio murmur" voices. Calls here
 * only post small commands to a lock-free queue, so they are cheap and safe
 * to make from the game loop.
 */
#ifndef LL_AUDIO_H
#define LL_AUDIO_H

#include <stdbool.h>
#include "world/world.h"

typedef enum { SPEAKER_DOT, SPEAKER_THEO } speaker_t;
typedef enum {
	UI_TICK, UI_PAGE, UI_CALL_CHIRP, UI_KEY_UP, UI_SQUELCH,
	UI_CHIMES,           /* Pell's spoon chimes (out of tune) */
	UI_STAIR_NOTE,       /* the singing stair: a slightly flat E-flat */
	UI_KETTLE,           /* the kettle comes to the boil */
	UI_THUNDER,          /* distant thunder (gain = how close) */
	UI_LAMP,             /* lamp switch */
	UI_SOUND_COUNT
} ui_sound_t;

void audio_init(void);
void audio_footstep(surface_t surf, float intensity, float pan);
void audio_ui(ui_sound_t s);
void audio_ui_gain(ui_sound_t s, float gain);
void audio_voice_line(const char *text, speaker_t who);   /* murmur voice for one line */
void audio_voice_stop(void);
void audio_radio_hiss(bool on);                            /* carrier hiss while the handset is up */
/* Per-frame scene parameters (0..1). */
void audio_set_scene(float wind, float birds, float night, float altitude, float listen_duck);
/* Rain on the land (0..1) and how enclosed the listener is (0 outdoors, 1 in a room). */
void audio_set_weather(float rain, float indoor);

/* Seconds the murmur takes to say a line (pure, shared with the dialogue pacing). */
float voice_line_seconds(const char *text, speaker_t who);

#endif
