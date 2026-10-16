/*
 * music.c -- the sequencer and the instruments that play the scores.
 *
 * Guitar: Karplus-Strong. A delay line one period long is filled with a
 * filtered noise burst (the pluck) and fed back through a two-point average
 * and a first-order all-pass that tunes the fractional part of the period.
 * A pair of low resonators gives it a wooden body.
 * Harmonics: a sine with a touch of its octave, bell-like decay.
 * Piano: six slightly stretched partials (rotating phasors, so no sinf per
 * sample), each decaying at its own rate, a felt-hammer thump, and a damper
 * when the key lifts.
 * Drone and pad: two detuned saws through a one-pole low-pass, slow swells.
 * Everything goes through a small Schroeder reverb: guitar a little, the
 * "distant" piano and the harmonics more.
 */
#include <math.h>
#include <string.h>
#include "audio/music.h"
#include "audio/dsp.h"
#include "core/mathx.h"
#include "music_data.h"

#define KS_MAX 1024
#define N_KS 10
#define N_PIANO 10
#define N_BELL 8
#define N_SAW 6
#define PARTIALS 6
#define MASTER 0.55f

static float mtof(int m) { return 440.0f * powf(2.0f, (m - 69) / 12.0f); }

typedef struct {
	bool on;
	int wait;                    /* samples before it sounds (humanised timing) */
	float buf[KS_MAX];
	int len, pos;
	float ap_c, ap_x1, ap_y1, last, loss;
	float t, life, damp_at, gain, pan;
} ks_t;

typedef struct {
	bool on;
	int wait;
	float re, im, c, s;          /* phasor */
	float env, k, gain, pan, t;
} bell_t;

typedef struct {
	bool on;
	int wait, note, np;
	float re[PARTIALS], im[PARTIALS], c[PARTIALS], s[PARTIALS], env[PARTIALS], k[PARTIALS];
	float t, off_at, rel_k, rel, gain, pan;
	svf_t hammer;
	noise_t rng;
} piano_t;

typedef struct {
	bool on;
	int voice, note;
	float p0, p1, inc0, inc1, env, attack, release, end_at, t, gain, pan;
	lp1_t lp;
	float lp_c;
} saw_t;

static ks_t s_ks[N_KS];
static bell_t s_bell[N_BELL];
static piano_t s_piano[N_PIANO];
static saw_t s_saw[N_SAW];

/* Guitar body and the reverb. */
static reson_t s_body[2];
static lp1_t s_gtone[2];
#define NCOMB 4
static const int COMB_LEN[NCOMB] = { 810, 862, 927, 984 };
static const int AP_LEN[2][2] = { { 403, 320 }, { 426, 343 } };
static float s_comb_buf[NCOMB][1000], s_comb_lp[NCOMB];
static int s_comb_pos[NCOMB];
static float s_ap_buf[2][2][440];
static int s_ap_pos[2][2];

static struct {
	int piece, pending;
	bool resolve, pending_resolve;
	int order[192], norder, oi, ev;
	float beat, beats_per_sample, sec_per_beat;
	float gain, fade;            /* fade: per-sample gain change when stopping (< 0) */
	noise_t rng;
} Q;

int music_count(void) { return MUS_NUM_PIECES; }
const char *music_title(int p) { return p >= 0 && p < MUS_NUM_PIECES ? MUS_PIECES[p].title : ""; }

int music_find(const char *id)
{
	for (int i = 0; i < MUS_NUM_PIECES; i++)
		if (!strcmp(MUS_PIECES[i].id, id)) return i;
	return -1;
}

void music_init(unsigned seed)
{
	memset(&Q, 0, sizeof(Q));
	memset(s_ks, 0, sizeof(s_ks));
	memset(s_bell, 0, sizeof(s_bell));
	memset(s_piano, 0, sizeof(s_piano));
	memset(s_saw, 0, sizeof(s_saw));
	memset(s_comb_buf, 0, sizeof(s_comb_buf));
	memset(s_ap_buf, 0, sizeof(s_ap_buf));
	Q.piece = Q.pending = -1;
	Q.rng.s = seed | 1u;
	reson_set(&s_body[0], 104.0f, 45.0f);
	reson_set(&s_body[1], 212.0f, 70.0f);
}

/* ------------------------------------------------------------------------ */
/* Instruments                                                              */
/* ------------------------------------------------------------------------ */

static int humanise(void) { return (int)(noise_unit(&Q.rng) * 0.009f * SR); }

static void pluck(int note, float vel, float dur_s)
{
	ks_t *v = NULL;
	for (int i = 0; i < N_KS; i++)
		if (!s_ks[i].on) { v = &s_ks[i]; break; }
	if (!v) {                                    /* steal the oldest */
		v = &s_ks[0];
		for (int i = 1; i < N_KS; i++)
			if (s_ks[i].t > v->t) v = &s_ks[i];
	}
	float f = mtof(note);
	float period = SR / f;
	int len = (int)(period - 0.6f);
	if (len >= KS_MAX) len = KS_MAX - 1;
	if (len < 8) len = 8;
	float d = period - 0.5f - len;               /* the average adds half a sample */
	memset(v, 0, sizeof(*v));
	v->on = true;
	v->wait = humanise();
	v->len = len;
	v->ap_c = (1.0f - d) / (1.0f + d);
	float t60 = clampf(6.5f * powf(110.0f / f, 0.35f), 1.6f, 7.5f);
	v->loss = powf(0.001f, 1.0f / (f * t60));
	/* The pluck: noise, softened more for gentle notes, with its DC removed. */
	float bright = 0.18f + 0.55f * vel, lp = 0.0f, lp2 = 0.0f, mean = 0.0f;
	for (int i = 0; i < len; i++) {
		lp += bright * (noise_white(&Q.rng) - lp);
		lp2 += bright * (lp - lp2);
		v->buf[i] = lp2;
		mean += lp2;
	}
	mean /= len;
	float peak = 1e-6f;
	for (int i = 0; i < len; i++) {
		v->buf[i] -= mean;
		peak = fmaxf(peak, fabsf(v->buf[i]));
	}
	for (int i = 0; i < len; i++) v->buf[i] *= vel / peak;
	v->life = t60 * 1.1f;
	v->damp_at = fmaxf(dur_s + 1.6f, 2.0f);
	v->gain = 0.095f;
	v->pan = clampf((note - 57) / 30.0f, -0.4f, 0.4f) - 0.1f;
}

static void bell(int note, float vel)
{
	bell_t *v = NULL;
	for (int i = 0; i < N_BELL; i++)
		if (!s_bell[i].on) { v = &s_bell[i]; break; }
	if (!v) {
		v = &s_bell[0];
		for (int i = 1; i < N_BELL; i++)
			if (s_bell[i].t > v->t) v = &s_bell[i];
	}
	float w = 6.2831853f * mtof(note) / SR;
	memset(v, 0, sizeof(*v));
	v->on = true;
	v->wait = humanise();
	v->re = 1.0f;
	v->c = cosf(w);
	v->s = sinf(w);
	v->env = 0.0f;
	v->k = expf(-6.9f / (3.4f * SR));
	v->gain = vel * 0.36f;
	v->pan = 0.25f - 0.5f * noise_unit(&Q.rng);
}

static void strike(int note, float vel, float dur_s)
{
	piano_t *v = NULL;
	for (int i = 0; i < N_PIANO; i++)
		if (!s_piano[i].on) { v = &s_piano[i]; break; }
	if (!v) {
		v = &s_piano[0];
		for (int i = 1; i < N_PIANO; i++)
			if (s_piano[i].t > v->t) v = &s_piano[i];
	}
	static const float AMP[PARTIALS] = { 1.0f, 0.42f, 0.2f, 0.11f, 0.06f, 0.035f };
	float f = mtof(note);
	float t60 = clampf(7.5f * powf(130.0f / f, 0.45f), 1.8f, 10.0f);
	float soft = 0.35f + 0.65f * vel;
	memset(v, 0, sizeof(*v));
	v->on = true;
	v->wait = humanise();
	v->note = note;
	v->rng.s = 0x51ED270Bu ^ (u32)note * 2654435761u;
	for (int n = 0; n < PARTIALS; n++) {
		float fn = f * (n + 1) * sqrtf(1.0f + 0.00035f * (n + 1) * (n + 1));
		if (fn > 9000.0f) break;
		float w = 6.2831853f * fn / SR;
		v->re[n] = 1.0f;
		v->im[n] = 0.0f;
		v->c[n] = cosf(w);
		v->s[n] = sinf(w);
		v->env[n] = AMP[n] * powf(soft, (float)n);
		v->k[n] = expf(-6.9f / (t60 / (1.0f + 0.7f * n) * SR));
		v->np = n + 1;
	}
	v->off_at = fmaxf(dur_s, 0.25f);
	v->rel_k = expf(-6.9f / (0.45f * SR));
	v->rel = 1.0f;
	v->gain = vel * 0.62f;
	v->pan = clampf((note - 60) / 36.0f, -0.35f, 0.35f) + 0.12f;
}

static void swell(int voice, int note, float vel, float dur_s)
{
	/* A drone note already sounding is held on, not restarted. */
	for (int i = 0; i < N_SAW; i++)
		if (s_saw[i].on && s_saw[i].voice == voice && s_saw[i].note == note && s_saw[i].t < s_saw[i].end_at) {
			s_saw[i].end_at = s_saw[i].t + dur_s;
			return;
		}
	saw_t *v = NULL;
	for (int i = 0; i < N_SAW; i++)
		if (!s_saw[i].on) { v = &s_saw[i]; break; }
	if (!v) return;
	float f = mtof(note);
	memset(v, 0, sizeof(*v));
	v->on = true;
	v->voice = voice;
	v->note = note;
	v->inc0 = f * 1.0025f / SR;
	v->inc1 = f * 0.9975f / SR;
	v->p1 = 0.37f;
	bool drone = voice == MV_DRONE;
	v->attack = drone ? 1.6f : 2.2f;
	v->release = drone ? 2.6f : 3.2f;
	v->end_at = dur_s;
	v->gain = vel * (drone ? 0.34f : 0.16f);
	v->lp_c = lp1_coef(drone ? 320.0f : 950.0f);
	v->pan = drone ? 0.0f : -0.2f;
}

static void trigger(const mus_event *e)
{
	float vel = clampf(e->vel / 255.0f * (0.94f + 0.12f * noise_unit(&Q.rng)), 0.05f, 1.0f);
	float dur = e->dur / (float)MUS_TICKS * Q.sec_per_beat;
	switch (e->voice) {
	case MV_GUITAR: pluck(e->note, vel, dur); break;
	case MV_HARMONIC: bell(e->note, vel); break;
	case MV_PIANO: strike(e->note, vel, dur); break;
	default: swell(e->voice, e->note, vel, dur); break;
	}
}

void music_debug_note(int voice, int note, float vel, float seconds)
{
	mus_event e = { 0, 0, (u8)note, (u8)voice, (u8)(vel * 255.0f) };
	Q.sec_per_beat = 1.0f;
	e.dur = (u16)(seconds * MUS_TICKS);
	Q.gain = 1.0f;
	trigger(&e);
}

/* ------------------------------------------------------------------------ */
/* Sequencer                                                                */
/* ------------------------------------------------------------------------ */

static void begin(int piece, bool resolve)
{
	const mus_piece *p = &MUS_PIECES[piece];
	Q.piece = piece;
	Q.resolve = resolve;
	Q.norder = 0;
	for (int b = 0; b < p->nbars && Q.norder < 192; b++) {
		int bi = p->first_bar + b;
		u8 fl = MUS_BARS[bi].flags;
		if ((fl & MUS_CODA) && !resolve) continue;
		if ((fl & MUS_OPTIONAL) && noise_unit(&Q.rng) < 0.3f) continue;
		Q.order[Q.norder++] = bi;
	}
	/* Bars marked '~' may trade places with the bar after them. */
	for (int i = 0; i + 1 < Q.norder; i++)
		if ((MUS_BARS[Q.order[i]].flags & MUS_SWAP) && !(MUS_BARS[Q.order[i + 1]].flags & MUS_CODA) &&
		    noise_unit(&Q.rng) < 0.35f) {
			int t = Q.order[i];
			Q.order[i] = Q.order[i + 1];
			Q.order[i + 1] = t;
			i++;
		}
	Q.oi = 0;
	Q.ev = 0;
	Q.beat = 0.0f;
	/* A breath of tempo variation, too. */
	float tempo = p->tempo * (0.97f + 0.06f * noise_unit(&Q.rng));
	Q.beats_per_sample = tempo / 60.0f / SR;
	Q.sec_per_beat = 60.0f / tempo;
	Q.gain = 1.0f;
	Q.fade = 0.0f;
}

void music_play(int piece, bool resolve)
{
	if (piece < 0 || piece >= MUS_NUM_PIECES) return;
	if (Q.piece >= 0 || music_active()) {
		/* Something is playing: let it fade, then start. */
		Q.pending = piece;
		Q.pending_resolve = resolve;
		if (Q.fade >= 0.0f) Q.fade = -1.0f / (2.5f * SR);
		return;
	}
	begin(piece, resolve);
}

void music_stop(float fade_seconds)
{
	Q.pending = -1;
	Q.fade = -1.0f / (fmaxf(fade_seconds, 0.05f) * SR);
}

bool music_active(void)
{
	if (Q.piece >= 0) return true;
	for (int i = 0; i < N_KS; i++)
		if (s_ks[i].on) return true;
	for (int i = 0; i < N_PIANO; i++)
		if (s_piano[i].on) return true;
	for (int i = 0; i < N_BELL; i++)
		if (s_bell[i].on) return true;
	for (int i = 0; i < N_SAW; i++)
		if (s_saw[i].on) return true;
	return false;
}

static void silence_all(void)
{
	for (int i = 0; i < N_KS; i++) s_ks[i].on = false;
	for (int i = 0; i < N_PIANO; i++) s_piano[i].on = false;
	for (int i = 0; i < N_BELL; i++) s_bell[i].on = false;
	for (int i = 0; i < N_SAW; i++) s_saw[i].on = false;
}

static void step_sequencer(void)
{
	if (Q.piece < 0) return;
	const mus_bar *b = &MUS_BARS[Q.order[Q.oi]];
	while (Q.ev < b->count && MUS_EVENTS[b->first + Q.ev].start <= Q.beat * MUS_TICKS) {
		trigger(&MUS_EVENTS[b->first + Q.ev]);
		Q.ev++;
	}
	Q.beat += Q.beats_per_sample;
	if (Q.beat >= b->beats) {
		Q.beat -= b->beats;
		Q.ev = 0;
		if (++Q.oi >= Q.norder) Q.piece = -1;     /* the last notes ring out */
	}
}

/* ------------------------------------------------------------------------ */
/* Rendering                                                                */
/* ------------------------------------------------------------------------ */

static float reverb(float in, float *out_r)
{
	static float dc = 1e-12f;
	dc = -dc;                    /* keeps the tails out of denormal range */
	in += dc;
	float acc = 0.0f;
	for (int k = 0; k < NCOMB; k++) {
		float *buf = s_comb_buf[k];
		int p = s_comb_pos[k];
		float y = buf[p];
		s_comb_lp[k] += 0.3f * (y - s_comb_lp[k]);
		buf[p] = in + s_comb_lp[k] * 0.8f;
		if (++p >= COMB_LEN[k]) p = 0;
		s_comb_pos[k] = p;
		acc += y;
	}
	acc *= 0.25f;
	float o[2];
	for (int ch = 0; ch < 2; ch++) {
		float x = acc;
		for (int a = 0; a < 2; a++) {
			float *buf = s_ap_buf[ch][a];
			int p = s_ap_pos[ch][a];
			float bo = buf[p];
			float y = -x * 0.5f + bo;
			buf[p] = x + bo * 0.5f;
			if (++p >= AP_LEN[ch][a]) p = 0;
			s_ap_pos[ch][a] = p;
			x = y;
		}
		o[ch] = x;
	}
	*out_r = o[1];
	return o[0];
}

void music_render(float *L, float *R, int n, float gain)
{
	if (Q.piece < 0 && !music_active()) {
		if (Q.pending >= 0) {
			int p = Q.pending;
			Q.pending = -1;
			begin(p, Q.pending_resolve);
		} else {
			return;
		}
	}
	const float dt = 1.0f / SR;
	float dn = 1e-12f;
	for (int i = 0; i < n; i++) {
		step_sequencer();
		dn = -dn;
		float gl = 0.0f, gr = 0.0f, dl = 0.0f, dr = 0.0f, send = 0.0f;

		/* Guitar. */
		float gsum = 0.0f;
		for (int k = 0; k < N_KS; k++) {
			ks_t *v = &s_ks[k];
			if (!v->on) continue;
			if (v->wait > 0) { v->wait--; continue; }
			float y = v->buf[v->pos];
			float avg = 0.5f * (y + v->last);
			v->last = y;
			float ap = v->ap_c * avg + v->ap_x1 - v->ap_c * v->ap_y1;
			v->ap_x1 = avg;
			v->ap_y1 = ap;
			v->buf[v->pos] = ap * v->loss;
			if (++v->pos >= v->len) v->pos = 0;
			v->t += dt;
			float e = v->t > v->damp_at ? fmaxf(0.0f, 1.0f - (v->t - v->damp_at) * 2.0f) : 1.0f;
			if (v->t > v->life || e <= 0.0f) v->on = false;
			float o = y * v->gain * e;
			gsum += o;
			gl += o * (1.0f - v->pan);
			gr += o * (1.0f + v->pan);
		}
		float body = reson_run(&s_body[0], gsum + dn) * 2.2f + reson_run(&s_body[1], gsum + dn) * 1.4f;
		gl = lp1(&s_gtone[0], gl + dn, 0.62f) + body;
		gr = lp1(&s_gtone[1], gr + dn, 0.62f) + body;
		dl += gl;
		dr += gr;
		send += (gl + gr) * 0.12f;

		/* Harmonics. */
		for (int k = 0; k < N_BELL; k++) {
			bell_t *v = &s_bell[k];
			if (!v->on) continue;
			if (v->wait > 0) { v->wait--; continue; }
			float re = v->re * v->c - v->im * v->s, im = v->re * v->s + v->im * v->c;
			v->re = re;
			v->im = im;
			v->t += dt;
			float att = 1.0f;
			if (v->t < 0.004f) {
				att = v->t / 0.004f;
				v->env = 1.0f;
			} else {
				v->env *= v->k;
			}
			float o = (im + 0.14f * 2.0f * im * re) * v->env * att * v->gain;
			if (v->t > 4.5f || (v->t > 0.1f && v->env < 2e-4f)) v->on = false;
			dl += o * (1.0f - v->pan);
			dr += o * (1.0f + v->pan);
			send += o * 0.5f;
		}

		/* Piano. */
		for (int k = 0; k < N_PIANO; k++) {
			piano_t *v = &s_piano[k];
			if (!v->on) continue;
			if (v->wait > 0) { v->wait--; continue; }
			float o = 0.0f;
			for (int p = 0; p < v->np; p++) {
				float re = v->re[p] * v->c[p] - v->im[p] * v->s[p], im = v->re[p] * v->s[p] + v->im[p] * v->c[p];
				v->re[p] = re;
				v->im[p] = im;
				v->env[p] *= v->k[p];
				o += im * v->env[p];
			}
			while (v->np > 1 && v->env[v->np - 1] < 1e-4f) v->np--;     /* top partials die first */
			float ham = v->t < 0.03f ? svf_bp(&v->hammer, noise_white(&v->rng), svf_coef(900.0f, 0.8f)) *
			                           (1.0f - v->t / 0.03f) * 0.18f : 0.0f;
			v->t += dt;
			if (v->t > v->off_at) v->rel *= v->rel_k;
			o = (o + ham) * v->rel * v->gain;
			if (v->rel < 0.001f || v->env[0] < 0.0005f) v->on = false;
			dl += o * (1.0f - v->pan);
			dr += o * (1.0f + v->pan);
			send += o * 0.55f;
		}

		/* Drone and pad. */
		for (int k = 0; k < N_SAW; k++) {
			saw_t *v = &s_saw[k];
			if (!v->on) continue;
			v->p0 += v->inc0;
			if (v->p0 >= 1.0f) v->p0 -= 1.0f;
			v->p1 += v->inc1;
			if (v->p1 >= 1.0f) v->p1 -= 1.0f;
			float x = (v->p0 + v->p1) - 1.0f;
			float y = lp1(&v->lp, x, v->lp_c);
			v->t += dt;
			float target = v->t < v->end_at ? 1.0f : 0.0f;
			float rate = target > v->env ? dt / v->attack : dt / v->release;
			v->env += clampf(target - v->env, -rate, rate);
			if (v->t > v->end_at && v->env <= 0.0005f) v->on = false;
			float o = y * v->env * v->gain;
			dl += o * (1.0f - v->pan);
			dr += o * (1.0f + v->pan);
			send += o * 0.3f;
		}

		float wr, wl = reverb(send, &wr);
		float g = Q.gain * gain * MASTER;
		L[i] += (dl + wl * 0.9f) * g;
		R[i] += (dr + wr * 0.9f) * g;

		if (Q.fade < 0.0f) {
			Q.gain += Q.fade;
			if (Q.gain <= 0.0f) {
				Q.gain = 0.0f;
				Q.fade = 0.0f;
				Q.piece = -1;
				silence_all();
				if (Q.pending >= 0) {
					int p = Q.pending;
					Q.pending = -1;
					begin(p, Q.pending_resolve);
				}
			}
		}
	}
	/* Keep the phasors on the unit circle. */
	for (int k = 0; k < N_BELL; k++) {
		bell_t *v = &s_bell[k];
		float m = 1.5f - 0.5f * (v->re * v->re + v->im * v->im);
		v->re *= m;
		v->im *= m;
	}
	for (int k = 0; k < N_PIANO; k++)
		for (int p = 0; p < s_piano[k].np; p++) {
			piano_t *v = &s_piano[k];
			float m = 1.5f - 0.5f * (v->re[p] * v->re[p] + v->im[p] * v->im[p]);
			v->re[p] *= m;
			v->im[p] *= m;
		}
}
