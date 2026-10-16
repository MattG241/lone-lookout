/*
 * noise.c -- value noise used for terrain shape and painterly colour washes.
 */
#include <math.h>
#include "core/noise.h"

u32 hash32(u32 x)
{
	x ^= x >> 16; x *= 0x7feb352dU;
	x ^= x >> 15; x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

u32 hash2(s32 x, s32 z, u32 seed)
{
	u32 h = (u32)x * 374761393U + (u32)z * 668265263U + seed * 2654435761U;
	h = (h ^ (h >> 13)) * 1274126177U;
	return h ^ (h >> 16);
}

static inline float h01(s32 x, s32 z, u32 seed)
{
	return (hash2(x, z, seed) >> 8) * (1.0f / 16777216.0f);
}

float noise2(float x, float z, u32 seed)
{
	float fx = floorf(x), fz = floorf(z);
	s32 ix = (s32)fx, iz = (s32)fz;
	float tx = x - fx, tz = z - fz;
	float u = tx * tx * (3.0f - 2.0f * tx);
	float v = tz * tz * (3.0f - 2.0f * tz);
	float a = h01(ix, iz, seed), b = h01(ix + 1, iz, seed);
	float c = h01(ix, iz + 1, seed), d = h01(ix + 1, iz + 1, seed);
	return (a + (b - a) * u) * (1.0f - v) + (c + (d - c) * u) * v;
}

float fbm2(float x, float z, int octaves, u32 seed)
{
	float sum = 0.0f, amp = 1.0f, norm = 0.0f, f = 1.0f;
	for (int i = 0; i < octaves; i++) {
		sum += amp * noise2(x * f, z * f, seed + (u32)i * 101u);
		norm += amp;
		amp *= 0.5f;
		f *= 2.0f;
	}
	return sum / norm;
}
