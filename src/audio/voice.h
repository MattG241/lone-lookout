/*
 * voice.h -- "radio murmur": a formant synthesiser that speaks a line's
 * rhythm, pitch contour and vowel colours without being intelligible --
 * like overhearing someone through a cheap handset in the next room.
 * Subtitles carry the words (DESIGN.md section 10).
 */
#ifndef LL_VOICE_H
#define LL_VOICE_H

#include <stdbool.h>
#include "audio/audio.h"
#include "audio/dsp.h"

#define VOICE_MAX_SYL 120

typedef struct {
	float start, dur;
	float f0;
	u8 vowel;           /* 0..4: a e i o u */
	u8 onset;           /* 0 none, 1 sibilant, 2 fricative, 3 plosive, 4 nasal/liquid */
} syl_t;

typedef struct {
	syl_t syl[VOICE_MAX_SYL];
	int n;
	float total;
	speaker_t who;
} voice_plan_t;

typedef struct {
	voice_plan_t plan;
	bool active;
	float t;
	int cur;
	float phase;
	float F[3];
	reson_t res[3];
	int coef_count;
	float env;
	float fade;          /* 1 while speaking; ramps to 0 on stop */
	bool stopping;
	noise_t rng;
	lp1_t tilt;
	float hp1x, hp1y, hp2x, hp2y;
	svf_t radio_lp, cons_f;
	bool garble;         /* wind or distance eating the signal */
} voice_t;

void voice_plan(const char *text, speaker_t who, voice_plan_t *out);
void voice_start(voice_t *v, const char *text, speaker_t who);
void voice_stop(voice_t *v);
void voice_render(voice_t *v, float *L, float *R, int frames);

#endif
