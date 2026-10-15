/*
 * dsp.h -- tiny inline DSP building blocks for the procedural sound engine.
 * Everything runs in float at 32 kHz on the mixer thread.
 */
#ifndef LL_DSP_H
#define LL_DSP_H

#include <math.h>
#include <gctypes.h>

#define SR 32000.0f

typedef struct { u32 s; } noise_t;

static inline float noise_white(noise_t *n)
{
	u32 x = n->s;
	x ^= x << 13; x ^= x >> 17; x ^= x << 5;
	n->s = x;
	return (float)(s32)x * (1.0f / 2147483648.0f);
}

static inline float noise_unit(noise_t *n) { return noise_white(n) * 0.5f + 0.5f; }

/* One-pole low-pass. coef = 1 - exp(-2 pi fc / SR). */
typedef struct { float y; } lp1_t;
static inline float lp1_coef(float fc) { return 1.0f - expf(-6.2831853f * fc / SR); }
static inline float lp1(lp1_t *f, float x, float c) { f->y += c * (x - f->y); return f->y; }

/* Chamberlin state-variable filter (stable for fc well below SR/6). */
typedef struct { float low, band; } svf_t;
typedef struct { float f, q; } svf_coef_t;
static inline svf_coef_t svf_coef(float fc, float res)
{
	svf_coef_t c;
	c.f = 2.0f * sinf(3.14159265f * fminf(fc, SR * 0.16f) / SR);
	c.q = 1.0f / res;
	return c;
}
static inline float svf_run(svf_t *s, float x, svf_coef_t c, float *band, float *high)
{
	s->low += c.f * s->band;
	float hi = x - s->low - c.q * s->band;
	s->band += c.f * hi;
	if (band) *band = s->band;
	if (high) *high = hi;
	return s->low;
}
static inline float svf_bp(svf_t *s, float x, svf_coef_t c)
{
	float b;
	svf_run(s, x, c, &b, 0);
	return b;
}

/* Two-pole resonator (formants, body resonances). */
typedef struct { float y1, y2, a1, a2, g; } reson_t;
static inline void reson_set(reson_t *r, float freq, float bw)
{
	float rad = expf(-3.14159265f * bw / SR);
	r->a1 = 2.0f * rad * cosf(6.2831853f * freq / SR);
	r->a2 = -rad * rad;
	r->g = (1.0f - rad * rad) * 0.5f;
}
static inline float reson_run(reson_t *r, float x)
{
	float y = r->g * x + r->a1 * r->y1 + r->a2 * r->y2;
	r->y2 = r->y1;
	r->y1 = y;
	return y;
}

static inline float soft_clip(float x) { return x / (1.0f + fabsf(x)); }

#endif
