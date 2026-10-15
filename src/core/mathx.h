/*
 * mathx.h -- small vector and scalar helpers used everywhere.
 *
 * World convention: x = east, y = up, z = south (so north is -z).
 * Angles are radians unless a name says _deg.
 */
#ifndef LL_MATHX_H
#define LL_MATHX_H

#include <math.h>

#define LL_PI 3.14159265358979f
#define LL_TAU (2.0f * LL_PI)
#define DEG2RAD(d) ((d) * (LL_PI / 180.0f))
#define RAD2DEG(r) ((r) * (180.0f / LL_PI))

typedef struct { float x, y, z; } v3;

static inline v3 v3_make(float x, float y, float z) { v3 r = { x, y, z }; return r; }
static inline v3 v3_add(v3 a, v3 b) { return v3_make(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline v3 v3_sub(v3 a, v3 b) { return v3_make(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline v3 v3_scale(v3 a, float s) { return v3_make(a.x * s, a.y * s, a.z * s); }
static inline float v3_dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline v3 v3_cross(v3 a, v3 b)
{
	return v3_make(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float v3_len(v3 a) { return sqrtf(v3_dot(a, a)); }
static inline v3 v3_norm(v3 a)
{
	float l = v3_len(a);
	return l > 1e-6f ? v3_scale(a, 1.0f / l) : v3_make(0, 1, 0);
}
static inline v3 v3_lerp(v3 a, v3 b, float t)
{
	return v3_make(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float saturatef(float v) { return clampf(v, 0.0f, 1.0f); }
static inline float smoothstepf(float e0, float e1, float x)
{
	float t = saturatef((x - e0) / (e1 - e0));
	return t * t * (3.0f - 2.0f * t);
}
/* Frame-rate independent exponential approach toward a target. */
static inline float approachf(float cur, float target, float rate, float dt)
{
	return target + (cur - target) * expf(-rate * dt);
}
static inline float wrap_anglef(float a)
{
	while (a > LL_PI) a -= LL_TAU;
	while (a < -LL_PI) a += LL_TAU;
	return a;
}
static inline float signf(float v) { return v < 0.0f ? -1.0f : 1.0f; }

/* Colour helpers: palette colours are float RGB in [0,1]. */
typedef struct { float r, g, b; } rgbf;
static inline rgbf rgb_make(float r, float g, float b) { rgbf c = { r, g, b }; return c; }
static inline rgbf rgb_from(const float *p) { return rgb_make(p[0], p[1], p[2]); }
static inline rgbf rgb_lerp(rgbf a, rgbf b, float t)
{
	return rgb_make(lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t));
}
static inline rgbf rgb_scale(rgbf a, float s) { return rgb_make(a.r * s, a.g * s, a.b * s); }
static inline rgbf rgb_mul(rgbf a, rgbf b) { return rgb_make(a.r * b.r, a.g * b.g, a.b * b.b); }
static inline rgbf rgb_add(rgbf a, rgbf b) { return rgb_make(a.r + b.r, a.g + b.g, a.b + b.b); }

#endif
