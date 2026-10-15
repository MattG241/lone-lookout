/*
 * audio.c -- the procedural sound engine.
 *
 * A mixer thread renders 512-frame stereo blocks (16 ms at 32 kHz) into a
 * small ring; an AESND streaming voice plays them. The DSP interrupt only
 * hands over a buffer that is already mixed and wakes the thread -- no
 * synthesis ever runs in interrupt context.
 *
 * Sound set: wind (with gusts, stronger up high, muffled indoors), rain,
 * birdsong by day, crickets at night, footsteps per surface (grass, dirt,
 * rock, wood, stairs, needles, ash, scree), radio chirp/key/squelch/hiss,
 * the murmur voices, and a few things in the world: the spoon chimes, the
 * singing stair, the kettle, thunder and the lamp. Music arrives in Phase 4.
 */
#include <string.h>
#include <malloc.h>
#include <gccore.h>
#include <aesndlib.h>
#include "audio/audio.h"
#include "audio/dsp.h"
#include "audio/voice.h"

#define BLOCK 512
#define NBUF 4
#define CMD_RING 64
#define MAX_SFX 12
#define STACK_SIZE (64 * 1024)

/* ---- command queue (game thread -> mixer thread), single producer/consumer ---- */
typedef enum { C_FOOT, C_UI, C_VOICE, C_VOICE_STOP, C_HISS } cmd_type;
typedef struct {
	u8 type, a;
	float f0, f1;
	char text[200];
} cmd_t;

static cmd_t s_cmds[CMD_RING];
static volatile u32 s_cmd_w, s_cmd_r;

/* Scene parameters written by the game thread (word writes are atomic). */
static volatile float s_wind = 0.5f, s_birds = 1.0f, s_night, s_alt, s_duck, s_rain, s_indoor;

/* ---- output ring ---- */
static s16 s_out[NBUF][BLOCK * 2] ATTRIBUTE_ALIGN(32);
static s16 s_silence[BLOCK * 2] ATTRIBUTE_ALIGN(32);
static volatile u8 s_state[NBUF];        /* 0 free, 1 mixed, 2 handed to DSP */
static volatile int s_give, s_fill;
static volatile int s_hist[2] = { -1, -1 };
static AESNDPB *s_voice;
static lwpq_t s_wake;
static lwp_t s_thread;
static u8 *s_stack;
static bool s_ok;

/* ---- synthesis state (mixer thread only) ---- */
typedef struct {
	bool on;
	int kind;            /* surface_t, or 100 + ui_sound_t */
	float t, len, gain, pan;
	float p0, p1;        /* oscillator phases */
	svf_t f1, f2;
	noise_t rng;
	float pitch;
} sfx_t;

static sfx_t s_sfx[MAX_SFX];
static voice_t s_vo;
static noise_t s_rng = { 0xC0FFEEu };
static svf_t s_wind_f[2], s_whistle_f, s_hiss_f, s_needle_f, s_rain_f[2];
static lp1_t s_muffle[2];
static float s_gust, s_gust_target, s_gust_timer;
static float s_hiss_level, s_hiss_target;
static struct {
	bool on;
	float t, len, f, f2, phase, gap_t, pan, amp, buzz;
	int notes_left;
	int species;
} s_bird;
static float s_bird_wait = 3.0f;
static float s_cricket_phase, s_cricket_t;

/* ======================================================================== */

static void post(const cmd_t *c)
{
	if (!s_ok) return;
	u32 w = s_cmd_w;
	if (w - s_cmd_r >= CMD_RING) return;            /* full: drop (never block the game) */
	s_cmds[w % CMD_RING] = *c;
	__asm__ volatile("sync" ::: "memory");
	s_cmd_w = w + 1;
}

void audio_footstep(surface_t surf, float intensity, float pan)
{
	cmd_t c = { C_FOOT, (u8)surf, intensity, pan, { 0 } };
	post(&c);
}

void audio_ui(ui_sound_t s)
{
	cmd_t c = { C_UI, (u8)s, 1.0f, 0, { 0 } };
	post(&c);
}

void audio_ui_gain(ui_sound_t s, float gain)
{
	cmd_t c = { C_UI, (u8)s, gain, 0, { 0 } };
	post(&c);
}

void audio_ui_pan(ui_sound_t s, float gain, float pan)
{
	cmd_t c = { C_UI, (u8)s, gain, pan, { 0 } };
	post(&c);
}

void audio_music_cue(const char *piece) { (void)piece; }

void audio_set_weather(float rain, float indoor)
{
	s_rain = rain;
	s_indoor = indoor;
}

void audio_voice_line(const char *text, speaker_t who)
{
	audio_voice_line_ex(text, who, false);
}

void audio_voice_line_ex(const char *text, speaker_t who, bool garbled)
{
	cmd_t c = { C_VOICE, (u8)who, garbled ? 1.0f : 0.0f, 0, { 0 } };
	strncpy(c.text, text, sizeof(c.text) - 1);
	post(&c);
}

void audio_voice_stop(void)
{
	cmd_t c = { C_VOICE_STOP, 0, 0, 0, { 0 } };
	post(&c);
}

void audio_radio_hiss(bool on)
{
	cmd_t c = { C_HISS, on ? 1 : 0, 0, 0, { 0 } };
	post(&c);
}

void audio_set_scene(float wind, float birds, float night, float altitude, float listen_duck)
{
	s_wind = wind;
	s_birds = birds;
	s_night = night;
	s_alt = altitude;
	s_duck = listen_duck;
}

/* ======================================================================== */

static sfx_t *sfx_alloc(void)
{
	for (int i = 0; i < MAX_SFX; i++)
		if (!s_sfx[i].on) {
			memset(&s_sfx[i], 0, sizeof(sfx_t));
			s_sfx[i].on = true;
			s_sfx[i].rng.s = 0x9E3779B9u ^ (u32)(i * 7919);
			return &s_sfx[i];
		}
	return 0;
}

static void handle(const cmd_t *c)
{
	switch (c->type) {
	case C_FOOT: {
		sfx_t *s = sfx_alloc();
		if (!s) break;
		s->kind = c->a;
		s->gain = c->f0;
		s->pan = c->f1;
		s->len = (c->a == SURF_WOOD || c->a == SURF_STAIR) ? 0.16f : (c->a == SURF_SCREE ? 0.16f : 0.11f);
		s->pitch = 0.9f + 0.2f * noise_unit(&s_rng);
		break;
	}
	case C_UI: {
		sfx_t *s = sfx_alloc();
		if (!s) break;
		if (c->a >= UI_SOUND_COUNT) break;
		s->kind = 100 + c->a;
		s->gain = c->f0;
		static const float lens[UI_SOUND_COUNT] = { 0.05f, 0.22f, 0.30f, 0.06f, 0.20f, 2.6f, 1.4f, 3.2f, 5.5f, 0.08f,
		                                             4.2f, 0.35f, 0.45f, 3.0f };
		s->len = lens[c->a];
		s->pan = c->f1;
		s->pitch = 0.97f + 0.06f * noise_unit(&s_rng);
		break;
	}
	case C_VOICE:
		voice_start(&s_vo, c->text, (speaker_t)c->a);
		s_vo.garble = c->f0 > 0.5f;
		break;
	case C_VOICE_STOP:
		voice_stop(&s_vo);
		break;
	case C_HISS:
		s_hiss_target = c->a ? 1.0f : 0.0f;
		break;
	}
}

/* One footstep / UI sound sample. Returns mono. */
static float sfx_sample(sfx_t *s, float dt)
{
	float t = s->t;
	float n = noise_white(&s->rng);
	float out = 0.0f;
	float e;
	switch (s->kind) {
	case SURF_GRASS: {
		e = expf(-t * 38.0f) * (1.0f - expf(-t * 900.0f));
		float b = svf_bp(&s->f1, n, svf_coef(2400.0f * s->pitch, 0.8f));
		float crk = noise_unit(&s->rng) > 0.985f ? n * 2.0f : 0.0f;
		out = (b * 0.9f + crk * 0.25f) * e;
		break;
	}
	case SURF_DIRT:
		e = expf(-t * 45.0f) * (1.0f - expf(-t * 700.0f));
		out = svf_bp(&s->f1, n, svf_coef(850.0f * s->pitch, 0.9f)) * e * 1.1f;
		break;
	case SURF_ROCK:
		e = expf(-t * 70.0f);
		out = svf_bp(&s->f1, n, svf_coef(3400.0f * s->pitch, 1.4f)) * e;
		break;
	case SURF_WOOD:
	case SURF_STAIR: {
		e = expf(-t * 30.0f);
		s->p0 += (105.0f * s->pitch) / SR;
		float thump = sinf(6.2831853f * s->p0) * e;
		float click = n * expf(-t * 400.0f) * 0.4f;
		float body = svf_bp(&s->f1, thump + click, svf_coef(380.0f * s->pitch, 2.5f));
		out = thump * 0.9f + click + body * 0.6f;
		break;
	}
	case SURF_NEEDLES:
		/* Soft and muffled: a duff of needles over dirt. */
		e = expf(-t * 34.0f) * (1.0f - expf(-t * 500.0f));
		out = (svf_bp(&s->f1, n, svf_coef(1150.0f * s->pitch, 0.7f)) * 0.8f +
		       svf_bp(&s->f2, n, svf_coef(3000.0f * s->pitch, 1.0f)) * 0.15f) * e;
		break;
	case SURF_ASH:
		/* Powdery: low, breathy, almost nothing. */
		e = expf(-t * 26.0f) * (1.0f - expf(-t * 300.0f));
		out = svf_bp(&s->f1, n, svf_coef(620.0f * s->pitch, 0.6f)) * e * 0.8f;
		break;
	case SURF_SCREE: {
		/* Gravel: a crunch and a few stones settling after. */
		e = expf(-t * 40.0f);
		float clicks = noise_unit(&s->rng) > 0.993f - 0.01f * expf(-t * 20.0f) ? n * 2.5f : 0.0f;
		out = svf_bp(&s->f1, n, svf_coef(2800.0f * s->pitch, 1.2f)) * e +
		      svf_bp(&s->f2, clicks, svf_coef(4200.0f, 2.0f)) * 0.5f;
		break;
	}
	case 100 + UI_CHIMES: {
		/* Five bent spoons: inharmonic partials, each a little out of tune. */
		static const float f[5] = { 1318.0f, 1489.0f, 1702.0f, 1911.0f, 2219.0f };
		float acc = 0.0f;
		for (int k = 0; k < 5; k++) {
			float st = t - k * 0.11f - 0.03f * (k & 1);
			if (st < 0.0f) continue;
			float ph = st * f[k] * s->pitch;
			acc += (sinf(6.2831853f * ph) + 0.3f * sinf(6.2831853f * ph * 2.76f)) * expf(-st * (2.2f + k * 0.4f));
		}
		out = acc * 0.06f;
		break;
	}
	case 100 + UI_STAIR_NOTE: {
		/* A tread that rings when stepped on: E-flat (311 Hz), a shade flat. */
		float f0 = 306.0f;
		e = expf(-t * 3.2f) * (1.0f - expf(-t * 300.0f));
		s->p0 += f0 / SR;
		s->p1 += f0 * 2.71f / SR;
		out = (sinf(6.2831853f * s->p0) + 0.35f * sinf(6.2831853f * s->p1) * expf(-t * 6.0f)) * e * 0.16f;
		out += n * expf(-t * 80.0f) * 0.08f;
		break;
	}
	case 100 + UI_KETTLE: {
		/* Breath first, then the whistle climbs and wavers. */
		float rise = smoothstepf(0.3f, 1.6f, t), fade = 1.0f - smoothstepf(2.6f, 3.2f, t);
		float f = 1800.0f + 700.0f * rise + 30.0f * sinf(t * 31.0f);
		s->p0 += f / SR;
		float breath = svf_bp(&s->f1, n, svf_coef(f, 2.0f)) * (1.0f - rise * 0.6f);
		out = (sinf(6.2831853f * s->p0) * rise * 0.7f + breath * 0.8f) * fade * 0.07f;
		break;
	}
	case 100 + UI_THUNDER: {
		/* Distant thunder: a crack (if close), then a long rolling rumble. */
		float roll = expf(-t * 0.9f) * (1.0f - expf(-t * 8.0f)) * (0.7f + 0.3f * sinf(t * 5.3f) * sinf(t * 2.1f));
		float low = svf_run(&s->f1, n, svf_coef(70.0f + 50.0f * s->gain, 0.9f), 0, 0);
		float mid = svf_bp(&s->f2, n, svf_coef(240.0f, 0.6f)) * expf(-t * 3.0f) * s->gain;
		out = (low * 2.2f + mid * 0.6f) * roll * 0.8f;
		break;
	}
	case 100 + UI_ELK: {
		/* A bull's bugle: a low grunt, a long rising whistle that breaks, then grunts. */
		float f;
		if (t < 0.5f) f = 220.0f + 260.0f * (t / 0.5f);
		else if (t < 2.2f) f = 480.0f + 900.0f * smoothstepf(0.5f, 1.6f, t) + 40.0f * sinf(t * 37.0f);
		else f = 380.0f - 120.0f * (t - 2.2f);
		s->p0 += f * s->pitch / SR;
		s->p1 += f * 2.01f * s->pitch / SR;
		float tone = sinf(6.2831853f * s->p0) + 0.35f * sinf(6.2831853f * s->p1);
		float breath = svf_bp(&s->f1, n, svf_coef(f * 1.5f, 1.2f));
		e = smoothstepf(0.0f, 0.08f, t) * (1.0f - smoothstepf(2.4f, 2.6f, t));
		float grunt = t > 2.7f ? svf_bp(&s->f2, n, svf_coef(160.0f, 2.0f)) *
		                         (sinf((t - 2.7f) * 14.0f) > 0.2f ? 1.0f : 0.0f) * expf(-(t - 2.7f) * 1.5f) : 0.0f;
		out = (tone * 0.55f + breath * 0.5f) * e * 0.12f + grunt * 0.25f;
		break;
	}
	case 100 + UI_STONE:
		e = expf(-t * 40.0f);
		out = svf_bp(&s->f1, n, svf_coef(1400.0f, 3.0f)) * e * 0.6f + (t > 0.12f ? svf_bp(&s->f2, n, svf_coef(2600.0f, 4.0f)) * expf(-(t - 0.12f) * 60.0f) * 0.3f : 0.0f);
		break;
	case 100 + UI_SHUTTER:
		e = expf(-t * 18.0f);
		s->p0 += 90.0f / SR;
		out = (sinf(6.2831853f * s->p0) * 0.6f + svf_bp(&s->f1, n, svf_coef(500.0f, 1.5f))) * e * 0.5f;
		break;
	case 100 + UI_VALVE: {
		float squeal = t < 0.4f ? sinf(6.2831853f * (s->p0 += (900.0f + 300.0f * t) / SR)) * 0.08f * (1.0f - t / 0.4f) : 0.0f;
		float water = svf_bp(&s->f1, n, svf_coef(700.0f + 300.0f * noise_unit(&s->rng), 0.8f)) *
		              smoothstepf(0.3f, 0.8f, t) * (1.0f - smoothstepf(2.4f, 3.0f, t)) * 0.4f;
		out = squeal + water;
		break;
	}
	case 100 + UI_LAMP:
		out = n * expf(-t * 300.0f) * 0.25f + svf_bp(&s->f1, n, svf_coef(3000.0f, 1.0f)) * expf(-t * 60.0f) * 0.2f;
		break;
	case 100 + UI_TICK:
		s->p0 += 1150.0f / SR;
		out = sinf(6.2831853f * s->p0) * expf(-t * 120.0f) * 0.35f + n * expf(-t * 900.0f) * 0.1f;
		break;
	case 100 + UI_PAGE: {
		float f = 1200.0f + 3200.0f * (t / s->len);
		e = sinf(3.14159265f * fminf(t / s->len, 1.0f));
		out = svf_bp(&s->f1, n, svf_coef(f, 1.1f)) * e * 0.35f;
		break;
	}
	case 100 + UI_CALL_CHIRP: {
		float f = t < 0.09f ? 1250.0f : (t > 0.13f ? 1650.0f : 0.0f);
		e = (t < 0.09f || t > 0.13f) ? 1.0f : 0.0f;
		e *= fminf(1.0f, (s->len - t) * 30.0f);
		s->p0 += f / SR;
		float sq = sinf(6.2831853f * s->p0);
		out = soft_clip(sq * 2.5f) * 0.16f * e;
		break;
	}
	case 100 + UI_KEY_UP:
		out = n * expf(-t * 120.0f) * 0.18f + (t < 0.004f ? 0.3f : 0.0f);
		break;
	case 100 + UI_SQUELCH:
		e = expf(-t * 16.0f);
		out = svf_bp(&s->f1, n, svf_coef(1900.0f, 0.7f)) * e * 0.5f;
		break;
	}
	s->t += dt;
	if (s->t >= s->len) s->on = false;
	return out;
}

static void start_bird(void)
{
	s_bird.on = true;
	s_bird.species = noise_unit(&s_rng) < 0.6f ? 0 : 1;
	s_bird.pan = noise_white(&s_rng) * 0.8f;
	s_bird.amp = 0.03f + 0.04f * noise_unit(&s_rng);
	if (s_bird.species == 0) {         /* varied-thrush-like: long buzzy whistles */
		static const float pitches[] = { 2350.0f, 2900.0f, 3600.0f, 4200.0f };
		s_bird.f = pitches[(int)(noise_unit(&s_rng) * 3.99f)];
		s_bird.len = 0.6f + 0.5f * noise_unit(&s_rng);
		s_bird.buzz = 85.0f + 40.0f * noise_unit(&s_rng);
		s_bird.notes_left = 0;
	} else {                           /* chickadee-like "fee-bee" */
		s_bird.f = 3900.0f;
		s_bird.f2 = 3350.0f;
		s_bird.len = 0.75f;
		s_bird.buzz = 0.0f;
	}
	s_bird.t = 0.0f;
}

static float bird_sample(float dt)
{
	if (!s_bird.on) return 0.0f;
	float t = s_bird.t, out;
	if (s_bird.species == 0) {
		float e = fminf(1.0f, t / 0.05f) * fminf(1.0f, (s_bird.len - t) / 0.18f);
		s_bird.phase += s_bird.f * (1.0f - 0.01f * t) / SR;
		float tone = sinf(6.2831853f * s_bird.phase);
		float buzz = 0.6f + 0.4f * sinf(6.2831853f * s_bird.buzz * t);
		out = tone * buzz * e;
	} else {
		bool first = t < 0.32f;
		float lt = first ? t : t - 0.4f;
		float e = (t < 0.32f || (t > 0.4f && t < s_bird.len)) ? fminf(1.0f, lt / 0.03f) * fminf(1.0f, (0.32f - lt) / 0.06f) : 0.0f;
		s_bird.phase += (first ? s_bird.f : s_bird.f2) / SR;
		out = sinf(6.2831853f * s_bird.phase) * fmaxf(e, 0.0f);
	}
	s_bird.t += dt;
	if (s_bird.t >= s_bird.len) s_bird.on = false;
	return out * s_bird.amp;
}

static void mix_block(s16 *dst)
{
	static float L[BLOCK], R[BLOCK];
	const float dt = 1.0f / SR;

	while (s_cmd_r != s_cmd_w) {
		handle(&s_cmds[s_cmd_r % CMD_RING]);
		s_cmd_r++;
	}

	/* Gusts: a slow random target, approached smoothly. */
	s_gust_timer -= BLOCK * dt;
	if (s_gust_timer <= 0.0f) {
		s_gust_target = noise_unit(&s_rng);
		s_gust_timer = 1.5f + 4.0f * noise_unit(&s_rng);
	}
	s_gust += (s_gust_target - s_gust) * 0.02f;
	float wind = s_wind * (0.55f + 0.45f * s_gust) * (1.0f + 0.6f * s_alt);
	svf_coef_t wc0 = svf_coef(320.0f + 420.0f * s_gust, 0.7f);
	svf_coef_t wc1 = svf_coef(360.0f + 380.0f * s_gust, 0.7f);
	svf_coef_t whc = svf_coef(880.0f + 500.0f * s_gust, 6.0f);
	svf_coef_t ndc = svf_coef(5200.0f, 0.6f);
	svf_coef_t hsc = svf_coef(1700.0f, 0.5f);
	s_hiss_level += (s_hiss_target - s_hiss_level) * 0.2f;
	float duck = 1.0f - 0.4f * s_duck;
	float indoor = s_indoor, rain = s_rain;
	wind *= 1.0f - 0.55f * indoor;
	svf_coef_t rc = svf_coef(indoor > 0.5f ? 2600.0f : 5200.0f, 0.5f);
	float muffle_c = lp1_coef(9000.0f - 7800.0f * indoor);

	if (!s_bird.on && s_birds > 0.05f) {
		s_bird_wait -= BLOCK * dt;
		if (s_bird_wait <= 0.0f) {
			start_bird();
			s_bird_wait = (2.0f + 7.0f * noise_unit(&s_rng)) / fmaxf(s_birds, 0.2f);
		}
	}

	for (int i = 0; i < BLOCK; i++) {
		float n0 = noise_white(&s_rng), n1 = noise_white(&s_rng);
		float w0 = svf_bp(&s_wind_f[0], n0, wc0), w1 = svf_bp(&s_wind_f[1], n1, wc1);
		float whistle = svf_bp(&s_whistle_f, n0, whc) * 0.25f * s_alt * s_gust;
		float hiss;
		svf_run(&s_needle_f, n1, ndc, 0, &hiss);
		hiss *= 0.05f * s_gust * s_gust;
		float l = (w0 * 0.55f + whistle + hiss) * wind * duck;
		float r = (w1 * 0.55f + whistle * 0.7f + hiss) * wind * duck;
		/* Rain: a soft bed outdoors; on a roof (indoors), brighter ticks. */
		l = lp1(&s_muffle[0], l, muffle_c);
		r = lp1(&s_muffle[1], r, muffle_c);
		if (rain > 0.01f) {
			float rl = svf_bp(&s_rain_f[0], noise_white(&s_rng), rc), rr = svf_bp(&s_rain_f[1], noise_white(&s_rng), rc);
			float drop = noise_unit(&s_rng) > 0.9985f - 0.002f * indoor ? noise_white(&s_rng) * (0.25f + 0.5f * indoor) : 0.0f;
			l += (rl * 0.09f + drop) * rain * duck;
			r += (rr * 0.09f + drop * 0.7f) * rain * duck;
		}

		float b = bird_sample(dt);
		l += b * (0.5f - 0.5f * s_bird.pan);
		r += b * (0.5f + 0.5f * s_bird.pan);

		if (s_night > 0.05f) {
			s_cricket_t += dt;
			s_cricket_phase += 4450.0f / SR;
			float pulse = sinf(6.2831853f * 32.0f * s_cricket_t) > 0.3f ? 1.0f : 0.0f;
			float chirp_env = fmodf(s_cricket_t, 1.1f) < 0.35f ? 1.0f : 0.0f;
			float cr = sinf(6.2831853f * s_cricket_phase) * pulse * chirp_env * 0.012f * s_night;
			l += cr * 0.8f;
			r += cr;
		}

		if (s_hiss_level > 0.001f) {
			float hs = svf_bp(&s_hiss_f, noise_white(&s_rng), hsc) * 0.022f * s_hiss_level;
			if (noise_unit(&s_rng) > 0.9993f) hs += noise_white(&s_rng) * 0.15f * s_hiss_level;
			l += hs;
			r += hs;
		}

		for (int k = 0; k < MAX_SFX; k++) {
			if (!s_sfx[k].on) continue;
			float v = sfx_sample(&s_sfx[k], dt) * s_sfx[k].gain * 0.5f;
			l += v * (1.0f - s_sfx[k].pan);
			r += v * (1.0f + s_sfx[k].pan);
		}
		L[i] = l;
		R[i] = r;
	}

	voice_render(&s_vo, L, R, BLOCK);

	for (int i = 0; i < BLOCK; i++) {
		float l = soft_clip(L[i] * 1.4f) * 0.9f, r = soft_clip(R[i] * 1.4f) * 0.9f;
		dst[i * 2 + 0] = (s16)(l * 32000.0f);
		dst[i * 2 + 1] = (s16)(r * 32000.0f);
	}
}

/* ======================================================================== */

static void voice_callback(AESNDPB *pb, u32 state)
{
	if (state != VOICE_STATE_STREAM) return;
	/* The buffer handed over two callbacks ago has certainly finished. */
	if (s_hist[1] >= 0) s_state[s_hist[1]] = 0;
	s_hist[1] = s_hist[0];
	int n = s_give;
	if (s_state[n] == 1) {
		AESND_SetVoiceBuffer(pb, s_out[n], sizeof(s_out[n]));
		s_state[n] = 2;
		s_hist[0] = n;
		s_give = (n + 1) % NBUF;
	} else {
		AESND_SetVoiceBuffer(pb, s_silence, sizeof(s_silence));   /* underrun: a gap, not a crash */
		s_hist[0] = -1;
	}
	LWP_ThreadSignal(s_wake);
}

static void *mixer_thread(void *arg)
{
	(void)arg;
	for (;;) {
		while (s_state[s_fill] == 0) {
			mix_block(s_out[s_fill]);
			DCFlushRange(s_out[s_fill], sizeof(s_out[s_fill]));
			s_state[s_fill] = 1;
			s_fill = (s_fill + 1) % NBUF;
		}
		LWP_ThreadSleep(s_wake);
	}
	return 0;
}

void audio_init(void)
{
	AESND_Init();
	memset(s_silence, 0, sizeof(s_silence));
	DCFlushRange(s_silence, sizeof(s_silence));
	LWP_InitQueue(&s_wake);
	s_ok = true;

	/* Pre-fill the whole ring before the voice starts. */
	for (int i = 0; i < NBUF; i++) {
		mix_block(s_out[i]);
		DCFlushRange(s_out[i], sizeof(s_out[i]));
		s_state[i] = 1;
	}
	s_fill = 0;

	s_voice = AESND_AllocateVoice(voice_callback);
	if (!s_voice) {
		s_ok = false;
		return;
	}
	AESND_SetVoiceFormat(s_voice, VOICE_STEREO16);
	AESND_SetVoiceFrequency(s_voice, (f32)SR);
	AESND_SetVoiceVolume(s_voice, 255, 255);
	AESND_SetVoiceStream(s_voice, true);
	AESND_SetVoiceBuffer(s_voice, s_out[0], sizeof(s_out[0]));
	s_state[0] = 2;
	s_hist[0] = 0;
	s_give = 1;

	s_stack = memalign(32, STACK_SIZE);
	LWP_CreateThread(&s_thread, mixer_thread, NULL, s_stack, STACK_SIZE, 80);
	AESND_SetVoiceStop(s_voice, false);
}
