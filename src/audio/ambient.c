/*
 * ambient.c -- synthesis for the sound log's beds (mixer thread).
 *
 * Every sound is built from noise, filters and a few oscillators, and is
 * only computed while it is audible. Calls (birds, frogs, the pika, the
 * woodpecker) are scheduled with random gaps so nothing repeats exactly.
 */
#include <math.h>
#include <string.h>
#include "audio/ambient.h"
#include "audio/dsp.h"
#include "core/mathx.h"

#define TAU 6.2831853f

static noise_t s_rng = { 0xA5A5F00Du };
static float s_gain[SND_COUNT], s_pan[SND_COUNT];

static float rnd(void) { return noise_unit(&s_rng); }
static float s_dn = 1e-12f;           /* flips sign every sample: keeps idle filters out of denormals */

/* Filter coefficients: fixed ones set once, the wind's per block (svf_coef costs a sinf). */
static svf_coef_t K_CREEK_LO, K_CREEK_HI, K_HOP[2], K_CREAK, K_TIN, K_SNAG;

/* A short decaying sine "ping" (bubbles, rain on tin, ticks). */
typedef struct { float ph, f, df, amp, k; } ping_t;

static float ping_run(ping_t *p)
{
	if (p->amp < 1e-4f) return 0.0f;
	p->ph += p->f / SR;
	if (p->ph >= 1.0f) p->ph -= 1.0f;
	p->f *= p->df;
	p->amp *= p->k;
	return sinf(TAU * p->ph) * p->amp;
}

static void ping_start(ping_t *p, float f, float rise, float amp, float decay_s)
{
	p->ph = 0.0f;
	p->f = f;
	p->df = rise;
	p->amp = amp;
	p->k = expf(-1.0f / (decay_s * SR));
}

/* --- S02 creek --------------------------------------------------------- */
static struct { svf_t lo, hi; ping_t b[4]; float wait; } C;

static float creek(void)
{
	float n = noise_white(&s_rng);
	float bed = svf_bp(&C.lo, n, K_CREEK_LO) * 0.22f + svf_bp(&C.hi, n, K_CREEK_HI) * 0.06f;
	if ((C.wait -= 1.0f / SR) <= 0.0f) {
		C.wait = 0.01f + 0.07f * rnd();
		ping_t *p = &C.b[(int)(rnd() * 3.99f)];
		ping_start(p, 500.0f + 1900.0f * rnd() * rnd(), 1.00004f + 0.00004f * rnd(), 0.05f + 0.07f * rnd(),
		           0.012f + 0.025f * rnd());
	}
	float bub = 0.0f;
	for (int i = 0; i < 4; i++) bub += ping_run(&C.b[i]);
	return bed + bub;
}

/* --- S03 grasshoppers -------------------------------------------------- */
static struct { svf_t f[2]; float rate[2], left[2], tick[2], amp[2]; } G;

static float grasshoppers(void)
{
	float out = 0.0f;
	for (int k = 0; k < 2; k++) {
		float imp = 0.0f;
		if (G.left[k] > 0.0f) {
			G.left[k] -= 1.0f / SR;
			if ((G.tick[k] -= 1.0f / SR) <= 0.0f) {
				G.tick[k] = 1.0f / G.rate[k];
				imp = G.amp[k] * (0.7f + 0.3f * rnd());
			}
		} else if (rnd() < 0.6f / SR) {         /* a new burst, every second or two */
			G.left[k] = 0.3f + 0.9f * rnd();
			G.rate[k] = 24.0f + 18.0f * rnd();
			G.amp[k] = 0.4f + 0.4f * rnd();
		}
		out += svf_bp(&G.f[k], imp + noise_white(&s_rng) * imp * 0.5f + s_dn, K_HOP[k]);
	}
	return out * 0.35f;
}

/* --- S05 sandpiper ----------------------------------------------------- */
static struct { float wait, t, ph; int notes; } SP;

static float sandpiper(void)
{
	if (SP.notes <= 0) {
		if ((SP.wait -= 1.0f / SR) <= 0.0f) {
			SP.notes = 2 * (1 + (int)(rnd() * 3.0f));
			SP.t = 0.0f;
			SP.wait = 4.0f + 7.0f * rnd();
		}
		return 0.0f;
	}
	/* "peet-weet": up-slurred whistles, 70 ms each, 60 ms apart. */
	SP.t += 1.0f / SR;
	float seg = 0.13f, local = fmodf(SP.t, seg);
	if (SP.t >= seg) {
		SP.t -= seg;
		SP.notes--;
	}
	if (local > 0.07f) return 0.0f;
	float f = (SP.notes & 1 ? 3300.0f : 2950.0f) + 600.0f * (local / 0.07f);
	SP.ph += f / SR;
	if (SP.ph >= 1.0f) SP.ph -= 1.0f;
	float e = sinf(3.14159265f * local / 0.07f);
	return sinf(TAU * SP.ph) * e * 0.12f;
}

/* --- S06 chorus frogs -------------------------------------------------- */
static struct { float wait[3], t[3], ph[3], len[3], pitch[3]; } FR;

static float frogs(void)
{
	float out = 0.0f;
	for (int k = 0; k < 3; k++) {
		if (FR.t[k] <= 0.0f) {
			if ((FR.wait[k] -= 1.0f / SR) <= 0.0f) {
				FR.t[k] = 1e-6f;
				FR.len[k] = 0.45f + 0.3f * rnd();
				FR.pitch[k] = 2300.0f + 500.0f * rnd();
				FR.wait[k] = 0.6f + 2.2f * rnd();
			}
			continue;
		}
		/* A rising "crreeek": a carrier pulsed ever faster, run a thumb down a comb. */
		float t = FR.t[k] += 1.0f / SR;
		if (t >= FR.len[k]) {
			FR.t[k] = 0.0f;
			continue;
		}
		float u = t / FR.len[k];
		float rate = 40.0f + 50.0f * u;
		float pulse = fmodf(t * rate, 1.0f) < 0.45f ? 1.0f : 0.0f;
		FR.ph[k] += FR.pitch[k] * (1.0f + 0.12f * u) / SR;
		if (FR.ph[k] >= 1.0f) FR.ph[k] -= 1.0f;
		out += sinf(TAU * FR.ph[k]) * pulse * sinf(3.14159265f * u) * 0.06f;
	}
	return out;
}

/* --- S07 pika ---------------------------------------------------------- */
static struct { float wait, t, ph; int left; } PK;

static float pika(void)
{
	if (PK.t <= 0.0f) {
		if ((PK.wait -= 1.0f / SR) <= 0.0f) {
			PK.t = 1e-6f;
			if (PK.left <= 0) PK.left = 1 + (rnd() < 0.35f);
			PK.wait = PK.left > 1 ? 0.25f : 5.0f + 10.0f * rnd();
		}
		return 0.0f;
	}
	float t = PK.t += 1.0f / SR;
	if (t > 0.13f) {
		PK.t = 0.0f;
		PK.left--;
		return 0.0f;
	}
	PK.ph += (2300.0f - 900.0f * t) / SR;
	if (PK.ph >= 1.0f) PK.ph -= 1.0f;
	float e = sinf(3.14159265f * t / 0.13f);
	return soft_clip(sinf(TAU * PK.ph) * 2.5f) * e * 0.11f;   /* a nasal, squared-off "eep" */
}

/* --- S08 wind in the snags --------------------------------------------- */
static struct { svf_t w, c; float gust, target, wait, creak_t, creak_ph; } SN;

static float snags(float wind)
{
	if ((SN.wait -= 1.0f / SR) <= 0.0f) {
		SN.target = rnd();
		SN.wait = 1.0f + 3.0f * rnd();
		if (rnd() < 0.25f) SN.creak_t = 1e-6f;
	}
	SN.gust += (SN.target - SN.gust) * 0.00004f;
	float n = noise_white(&s_rng);
	float whistle = svf_bp(&SN.w, n, K_SNAG) * (0.3f + 0.7f * SN.gust) * 0.28f;
	float creak = 0.0f;
	if (SN.creak_t > 0.0f) {
		float t = SN.creak_t += 1.0f / SR;
		if (t > 0.7f) SN.creak_t = 0.0f;
		SN.creak_ph += (85.0f + 30.0f * sinf(t * 9.0f)) / SR;
		if (SN.creak_ph >= 1.0f) SN.creak_ph -= 1.0f;
		float saw = SN.creak_ph * 2.0f - 1.0f;
		creak = svf_bp(&SN.c, saw * (fmodf(t * 23.0f, 1.0f) < 0.6f) + s_dn, K_CREAK) * sinf(3.14159265f * t / 0.7f) * 0.25f;
	}
	return (whistle + creak) * clampf(wind, 0.2f, 1.2f);
}

/* --- S09 woodpecker ---------------------------------------------------- */
static struct { reson_t r[2]; float wait, tick, rate; int strikes; } WP;

static float woodpecker(void)
{
	float imp = 0.0f;
	if (WP.strikes > 0) {
		if ((WP.tick -= 1.0f / SR) <= 0.0f) {
			WP.tick = 1.0f / WP.rate;
			WP.rate *= 0.975f;                   /* the roll slows as it goes */
			WP.strikes--;
			imp = 0.8f + 0.2f * rnd();
		}
	} else if ((WP.wait -= 1.0f / SR) <= 0.0f) {
		WP.strikes = 14 + (int)(rnd() * 8.0f);
		WP.rate = 17.0f;
		WP.tick = 0.0f;
		WP.wait = 5.0f + 8.0f * rnd();
		reson_set(&WP.r[0], 820.0f, 180.0f);
		reson_set(&WP.r[1], 1650.0f, 260.0f);
	}
	float x = imp + (imp > 0.0f ? noise_white(&s_rng) * 0.3f : 0.0f) + s_dn;
	return (reson_run(&WP.r[0], x) * 3.0f + reson_run(&WP.r[1], x) * 1.6f) * 0.5f;
}

/* --- S10 spoon chimes (on their own, in the wind) ---------------------- */
static struct { float wait, t[5], ph[5], ph2[5]; } CH;

static float chimes(float wind)
{
	static const float F[5] = { 1318.0f, 1489.0f, 1702.0f, 1911.0f, 2219.0f };
	if ((CH.wait -= 1.0f / SR) <= 0.0f) {
		CH.wait = (1.2f + 4.0f * rnd()) / clampf(wind, 0.3f, 1.5f);
		int k = (int)(rnd() * 4.99f);
		CH.t[k] = 1e-6f;
		if (rnd() < 0.4f) CH.t[(k + 2) % 5] = 1e-6f;
	}
	float out = 0.0f;
	for (int k = 0; k < 5; k++) {
		if (CH.t[k] <= 0.0f) continue;
		float t = CH.t[k] += 1.0f / SR;
		if (t > 2.4f) {
			CH.t[k] = 0.0f;
			continue;
		}
		CH.ph[k] += F[k] / SR;
		if (CH.ph[k] >= 1.0f) CH.ph[k] -= 1.0f;
		CH.ph2[k] += F[k] * 2.76f / SR;
		if (CH.ph2[k] >= 1.0f) CH.ph2[k] -= 1.0f;
		out += (sinf(TAU * CH.ph[k]) + 0.3f * sinf(TAU * CH.ph2[k])) * expf(-t * (2.2f + 0.4f * k));
	}
	return out * 0.05f;
}

/* --- S11 rain on the tin roof ------------------------------------------ */
/* Each drop rings a resonator (no oscillator to run): struck, then left to decay. */
static struct { svf_t bed; reson_t r[6]; int next; } TR;

static float tin_rain(void)
{
	float hit[6] = { 0 };
	if (rnd() < 140.0f / SR) {
		int k = TR.next++ % 6;
		float decay = 0.006f + 0.012f * rnd();
		reson_set(&TR.r[k], 2200.0f + 3800.0f * rnd(), 1.0f / (3.14159265f * decay));
		hit[k] = (0.05f + 0.08f * rnd()) * 40.0f;
	}
	float out = svf_bp(&TR.bed, noise_white(&s_rng), K_TIN) * 0.07f;
	for (int i = 0; i < 6; i++) out += reson_run(&TR.r[i], hit[i] + s_dn);
	return out;
}

/* --- S12 almost nothing ------------------------------------------------ */
static struct { ping_t p[2]; lp1_t hush; } NO;

static float nothing(void)
{
	if (rnd() < 1.3f / SR) ping_start(&NO.p[rnd() < 0.5f], 3000.0f + 4000.0f * rnd(), 1.0f, 0.02f, 0.004f);
	float hush = lp1(&NO.hush, noise_white(&s_rng), lp1_coef(180.0f)) * 0.05f;
	return hush + ping_run(&NO.p[0]) + ping_run(&NO.p[1]);
}

/* ------------------------------------------------------------------------ */

void ambient_init(void)
{
	memset(s_gain, 0, sizeof(s_gain));
	memset(s_pan, 0, sizeof(s_pan));
	SP.wait = 2.0f;
	PK.wait = 3.0f;
	WP.wait = 2.0f;
	FR.wait[1] = 0.7f;
	FR.wait[2] = 1.6f;
	K_CREEK_LO = svf_coef(620.0f, 0.6f);
	K_CREEK_HI = svf_coef(2600.0f, 1.0f);
	K_HOP[0] = svf_coef(5100.0f, 3.0f);
	K_HOP[1] = svf_coef(4300.0f, 3.0f);
	K_CREAK = svf_coef(320.0f, 2.0f);
	K_TIN = svf_coef(3200.0f, 0.7f);
	K_SNAG = svf_coef(560.0f, 9.0f);
}

void ambient_render(float *L, float *R, int n, const float *gain, const float *pan, float wind)
{
	/* Smooth the targets once per block: about 60 ms to settle. */
	bool any = false;
	for (int k = 0; k < SND_COUNT; k++) {
		s_gain[k] += (gain[k] - s_gain[k]) * 0.25f;
		s_pan[k] += (pan[k] - s_pan[k]) * 0.25f;
		if (s_gain[k] < 1e-4f && gain[k] <= 0.0f) s_gain[k] = 0.0f;
		any |= s_gain[k] > 0.0f;
	}
	if (!any) return;
	if (s_gain[SND_SNAGS] > 0.0f) K_SNAG = svf_coef(430.0f + 260.0f * SN.gust, 9.0f);
	for (int i = 0; i < n; i++) {
		float l = 0.0f, r = 0.0f;
		s_dn = -s_dn;
		for (int k = 0; k < SND_COUNT; k++) {
			float g = s_gain[k];
			if (g <= 0.0f) continue;
			float x;
			switch (k) {
			case SND_CREEK: x = creek(); break;
			case SND_GRASSHOPPERS: x = grasshoppers(); break;
			case SND_SANDPIPER: x = sandpiper(); break;
			case SND_FROGS: x = frogs(); break;
			case SND_PIKA: x = pika(); break;
			case SND_SNAGS: x = snags(wind); break;
			case SND_WOODPECKER: x = woodpecker(); break;
			case SND_CHIMES: x = chimes(wind); break;
			case SND_TIN_RAIN: x = tin_rain(); break;
			case SND_NOTHING: x = nothing(); break;
			default: continue;                  /* the stair and the elk are one-shots */
			}
			x *= g;
			l += x * (1.0f - s_pan[k]);
			r += x * (1.0f + s_pan[k]);
		}
		L[i] += l;
		R[i] += r;
	}
}
