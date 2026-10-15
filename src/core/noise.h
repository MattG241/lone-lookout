/*
 * noise.h -- deterministic hashing, value noise and a tiny RNG.
 *
 * The hash matches tools/worldgen/ashen_peaks.py closely enough that terrain
 * features built from the same recipes look alike in tools and in game.
 */
#ifndef LL_NOISE_H
#define LL_NOISE_H

#include <gctypes.h>

u32 hash32(u32 x);
u32 hash2(s32 x, s32 z, u32 seed);
float noise2(float x, float z, u32 seed);          /* smooth value noise in [0,1) */
float fbm2(float x, float z, int octaves, u32 seed); /* normalised fractal sum, ~[0,1] */

typedef struct { u32 s; } rng_t;
static inline void rng_seed(rng_t *r, u32 seed) { r->s = seed ? seed : 0x9E3779B9u; }
static inline u32 rng_next(rng_t *r)
{
	u32 x = r->s;
	x ^= x << 13; x ^= x >> 17; x ^= x << 5;
	return r->s = x;
}
static inline float rng_float(rng_t *r) { return (rng_next(r) >> 8) * (1.0f / 16777216.0f); }
static inline float rng_range(rng_t *r, float lo, float hi) { return lo + (hi - lo) * rng_float(r); }

#endif
