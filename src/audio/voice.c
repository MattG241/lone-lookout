/*
 * voice.c -- planning and synthesising murmur voices.
 *
 * Planning is pure and deterministic (no RNG state), so the dialogue system
 * on the game thread and the synth on the audio thread agree exactly on how
 * long a line lasts.
 */
#include <string.h>
#include "audio/voice.h"
#include "core/noise.h"

typedef struct {
	float f0, rate, formant_scale, breath, gain;
	bool radio;
} speaker_profile;

static const speaker_profile PROFILE[3] = {
	/* Dot: warm alto, quicker, heard through the handset. */
	{ 182.0f, 4.4f, 1.10f, 0.14f, 1.25f, true },
	/* Theo: soft light baritone, slower, heard in the room. */
	{ 104.0f, 3.8f, 0.94f, 0.10f, 0.85f, false },
	/* Harrow Crew: a tired tenor on a handheld, clipped and quick. */
	{ 132.0f, 4.9f, 0.98f, 0.22f, 1.15f, true },
};

/* Adult formants F1, F2, F3 (Hz) for a e i o u. */
static const float FORMANTS[5][3] = {
	{ 730, 1090, 2440 }, { 530, 1840, 2480 }, { 300, 2200, 2950 }, { 570, 840, 2410 }, { 320, 900, 2240 },
};

static int vowel_of(char c)
{
	switch (c | 32) {
	case 'a': return 0;
	case 'e': return 1;
	case 'i': case 'y': return 2;
	case 'o': return 3;
	case 'u': return 4;
	default: return -1;
	}
}

static int onset_of(char c)
{
	switch (c | 32) {
	case 's': case 'z': case 'c': case 'x': case 'j': return 1;
	case 'f': case 'v': case 'h': return 2;
	case 't': case 'p': case 'k': case 'b': case 'd': case 'g': case 'q': return 3;
	case 'm': case 'n': case 'l': case 'r': case 'w': return 4;
	default: return 0;
	}
}

static float jitter(u32 a, u32 b) { return (hash2((s32)a, (s32)b, 77u) >> 8) * (1.0f / 16777216.0f); }

void voice_plan(const char *text, speaker_t who, voice_plan_t *p)
{
	const speaker_profile *sp = &PROFILE[who];
	float t = 0.08f;
	float base = 1.0f / sp->rate;
	p->n = 0;
	p->who = who;
	int len = (int)strlen(text);
	int i = 0;
	while (i < len && p->n < VOICE_MAX_SYL - 1) {
		unsigned char c = (unsigned char)text[i];
		if (c >= 0x80) {                         /* em dash, ellipsis, quotes: a breath */
			int n = c >= 0xE0 ? 3 : 2;
			t += 0.24f;
			i += n;
			continue;
		}
		if (c == ',' || c == ';' || c == ':') { t += 0.22f; i++; continue; }
		if (c == '.' || c == '!' || c == '?') {
			if (c == '?' && p->n >= 2) {        /* questions rise at the end */
				p->syl[p->n - 1].f0 *= 1.22f;
				p->syl[p->n - 2].f0 *= 1.10f;
			}
			t += 0.30f;
			i++;
			continue;
		}
		if (vowel_of((char)c) < 0 && onset_of((char)c) == 0) { i++; continue; }

		/* A word: split into vowel groups. */
		int w0 = i;
		while (i < len && ((text[i] | 32) >= 'a' && (text[i] | 32) <= 'z')) i++;
		int w1 = i;
		int word_len = w1 - w0;
		int first = p->n;
		char prev = 0;
		for (int k = w0; k < w1 && p->n < VOICE_MAX_SYL - 1; k++) {
			int v = vowel_of(text[k]);
			if (v < 0) { prev = text[k]; continue; }
			/* swallow the rest of this vowel group */
			while (k + 1 < w1 && vowel_of(text[k + 1]) >= 0) k++;
			syl_t *s = &p->syl[p->n];
			float j1 = jitter((u32)k, (u32)p->n), j2 = jitter((u32)p->n, (u32)k * 3u);
			s->vowel = (u8)v;
			s->onset = (u8)onset_of(prev);
			s->dur = base * (0.82f + 0.36f * j1);
			s->start = t;
			s->f0 = sp->f0 * (1.0f + 0.05f * (j2 - 0.5f));
			t += s->dur;
			p->n++;
			prev = 0;
		}
		if (p->n == first && p->n < VOICE_MAX_SYL - 1) {   /* "hmm", "th" ... */
			syl_t *s = &p->syl[p->n++];
			s->vowel = 1;
			s->onset = 4;
			s->dur = base * 0.8f;
			s->start = t;
			s->f0 = sp->f0;
			t += s->dur;
		}
		/* Lengthen word ends a touch; stress long words' first syllable. */
		p->syl[p->n - 1].dur *= 1.15f;
		t += p->syl[p->n - 1].dur * 0.15f;
		if (word_len > 4) p->syl[first].f0 *= 1.06f;
		t += 0.035f;
	}
	/* Declination: pitch drifts down across the phrase. */
	for (int k = 0; k < p->n; k++) {
		float prog = t > 0.0f ? p->syl[k].start / t : 0.0f;
		p->syl[k].f0 *= 1.08f - 0.14f * prog;
	}
	p->total = t + 0.15f;
}

float voice_line_seconds(const char *text, speaker_t who)
{
	static voice_plan_t tmp;          /* game thread only */
	voice_plan(text, who, &tmp);
	return tmp.total;
}

void voice_start(voice_t *v, const char *text, speaker_t who)
{
	memset(v, 0, sizeof(*v));
	voice_plan(text, who, &v->plan);
	v->active = true;
	v->fade = 1.0f;
	v->rng.s = 0x1234567u + (u32)who * 977u;
	for (int i = 0; i < 3; i++) v->F[i] = FORMANTS[1][i];
}

void voice_start_pcm(voice_t *v, const s16 *pcm, int len, speaker_t who)
{
	memset(v, 0, sizeof(*v));
	v->plan.who = who;
	v->active = true;
	v->fade = 1.0f;
	v->rng.s = 0x7654321u + (u32)who * 977u;
	v->pcm = pcm;
	v->pcm_len = len;
}

void voice_stop(voice_t *v)
{
	if (v->active) v->stopping = true;
}

void voice_render(voice_t *v, float *L, float *R, int frames)
{
	if (!v->active) return;
	const speaker_profile *sp = &PROFILE[v->plan.who];
	const float dt = 1.0f / SR;
	svf_coef_t lp = svf_coef(3200.0f, 0.8f);
	svf_coef_t sib = svf_coef(5200.0f, 1.2f), fri = svf_coef(2100.0f, 0.9f);
	const float hp_a = 0.935f;                      /* ~330 Hz one-pole high-pass */
	const float tilt_c = lp1_coef(2600.0f);

	for (int s = 0; s < frames; s++) {
		v->t += dt;
		float out;
		if (v->pcm) {
			/* A recorded line: resample 22,050 -> 32,000 Hz, then the same handset chain. */
			int i0 = (int)v->pcm_pos;
			if (i0 + 1 >= v->pcm_len || (v->stopping && v->fade <= 0.0f)) {
				v->active = false;
				return;
			}
			if (v->stopping) v->fade -= dt * 25.0f;
			float fr = v->pcm_pos - i0;
			out = (v->pcm[i0] + (v->pcm[i0 + 1] - v->pcm[i0]) * fr) * (1.0f / 32768.0f) * 0.8f * v->fade;
			v->pcm_pos += VO_RATE / SR;
			goto chain;
		}
		while (v->cur < v->plan.n && v->t > v->plan.syl[v->cur].start + v->plan.syl[v->cur].dur)
			v->cur++;
		if ((v->cur >= v->plan.n && v->t > v->plan.total) || (v->stopping && v->fade <= 0.0f)) {
			v->active = false;
			return;
		}
		if (v->stopping) v->fade -= dt * 25.0f;

		float env_target = 0.0f, f0 = sp->f0, local = 0.0f;
		const syl_t *sy = 0;
		if (v->cur < v->plan.n && v->t >= v->plan.syl[v->cur].start) {
			sy = &v->plan.syl[v->cur];
			local = (v->t - sy->start) / sy->dur;
			env_target = 1.0f;
			if (local < 0.12f) env_target = local / 0.12f;
			if (local > 0.78f) env_target = (1.0f - local) / 0.22f;
			f0 = sy->f0 * (1.0f - 0.06f * local);
		}
		v->env += (env_target - v->env) * 0.01f;

		/* Formant glide toward the current vowel, coefficients every 16 samples. */
		if ((v->coef_count++ & 15) == 0) {
			int vw = sy ? sy->vowel : 1;
			for (int i = 0; i < 3; i++) {
				float target = FORMANTS[vw][i] * sp->formant_scale;
				v->F[i] += (target - v->F[i]) * 0.35f;
				reson_set(&v->res[i], v->F[i], 70.0f + 40.0f * i);
			}
		}

		/* Glottal pulse train with a little breath. */
		v->phase += f0 * (1.0f + 0.006f * sinf(v->t * 31.0f)) * dt;
		if (v->phase >= 1.0f) v->phase -= 1.0f;
		float g = v->phase < 0.6f ? sinf(3.14159265f * v->phase / 0.6f) : 0.0f;
		g = g * g - 0.3f;
		float nz = noise_white(&v->rng);
		float src = lp1(&v->tilt, g + nz * sp->breath, tilt_c);
		float voiced = reson_run(&v->res[0], src) * 1.0f + reson_run(&v->res[1], src) * 0.75f +
		               reson_run(&v->res[2], src) * 0.35f;
		voiced *= v->env * 3.2f;

		/* Consonant at the syllable onset. */
		float cons = 0.0f;
		if (sy && sy->onset && local < 0.22f) {
			float ce = 1.0f - local / 0.22f;
			switch (sy->onset) {
			case 1: { float hi; svf_run(&v->cons_f, nz, sib, 0, &hi); cons = hi * 0.22f * ce; break; }
			case 2: cons = svf_bp(&v->cons_f, nz, fri) * 0.35f * ce; break;
			case 3: cons = local < 0.06f ? nz * 0.30f * (1.0f - local / 0.06f) : 0.0f; break;
			default: voiced *= 0.6f + 0.4f * (1.0f - ce); break;
			}
		}
		out = (voiced + cons) * sp->gain * v->fade;

	chain:
		if (v->garble) {
			float nz = noise_white(&v->rng);
			/* The signal drops out in ragged 90 ms chunks, with crackle in the gaps. */
			u32 chunk = (u32)(v->t * 11.0f);
			if ((hash2((s32)chunk, 7, 991u) & 255) < 95) out = out * 0.04f + nz * 0.06f;
		}
		if (sp->radio) {
			/* Handset: band-limit to roughly 330-3200 Hz and lean into the clip. */
			float h1 = hp_a * (v->hp1y + out - v->hp1x);
			v->hp1x = out; v->hp1y = h1;
			float h2 = hp_a * (v->hp2y + h1 - v->hp2x);
			v->hp2x = h1; v->hp2y = h2;
			out = svf_run(&v->radio_lp, h2, lp, 0, 0);
			out = soft_clip(out * 2.2f) * 0.55f;
		}
		L[s] += out;
		R[s] += out;
	}
}
