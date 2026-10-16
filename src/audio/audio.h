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

typedef enum { SPEAKER_DOT, SPEAKER_THEO, SPEAKER_CREW } speaker_t;
typedef enum {
	UI_TICK, UI_PAGE, UI_CALL_CHIRP, UI_KEY_UP, UI_SQUELCH,
	UI_CHIMES,           /* Pell's spoon chimes (out of tune) */
	UI_STAIR_NOTE,       /* the singing stair: a slightly flat E-flat */
	UI_KETTLE,           /* the kettle comes to the boil */
	UI_THUNDER,          /* distant thunder (gain = how close) */
	UI_LAMP,             /* lamp switch */
	UI_ELK,              /* a bull elk's bugle, far off */
	UI_STONE,            /* a stone set on the cairn */
	UI_SHUTTER,          /* a wooden shutter swung to */
	UI_VALVE,            /* the cistern valve, and water running out */
	UI_PENCIL,           /* a sound written into the notebook */
	UI_SOUND_COUNT
} ui_sound_t;

void audio_init(void);
void audio_footstep(surface_t surf, float intensity, float pan);
void audio_ui(ui_sound_t s);
void audio_ui_gain(ui_sound_t s, float gain);
void audio_ui_pan(ui_sound_t s, float gain, float pan);     /* pan -1 left .. 1 right */
/* Music: start a piece by name ("channel", "settling", "tarn", "ash", "seven", "rain");
 * "<name>_end" plays it with its coda. A piece already playing fades out first. */
void audio_music_cue(const char *piece);
void audio_music_stop(float fade_seconds);
bool audio_music_playing(void);                           /* playing or ringing out */
/* The sound log's beds (ambient.h: sound_id): this frame's gain (0 silent) and pan. */
void audio_set_emitter(int id, float gain, float pan);
void audio_set_listen_focus(int id);                       /* the sound Listen is on, or -1 */
void audio_voice_line(const char *text, speaker_t who);   /* murmur voice for one line */
void audio_voice_line_ex(const char *text, speaker_t who, bool garbled);   /* wind eats syllables */
void audio_voice_stop(void);
/* A recorded line (16-bit mono, 22,050 Hz) through the handset chain; the
 * samples must stay valid until audio_voice_busy() goes false. */
void audio_voice_pcm(const s16 *pcm, int len, speaker_t who, bool garbled);
bool audio_voice_busy(void);
/* Options: volumes 0..1 (music; the world's sounds; the radio), mono, and the
 * left-ear effect while listening. */
void audio_set_options(float music, float world, float radio, bool mono, bool hearing);
/* Mixer health: buffers the DSP found unready, blocks mixed, average ms per block. */
void audio_stats(u32 *underruns, u32 *blocks, float *mix_ms);
void audio_radio_hiss(bool on);                            /* carrier hiss while the handset is up */
/* Per-frame scene parameters (0..1); listen is how far into Listen the player is. */
void audio_set_scene(float wind, float birds, float night, float altitude, float listen);
/* Rain on the land (0..1) and how enclosed the listener is (0 outdoors, 1 in a room). */
void audio_set_weather(float rain, float indoor);

/* Seconds the murmur takes to say a line (pure, shared with the dialogue pacing). */
float voice_line_seconds(const char *text, speaker_t who);

#endif
