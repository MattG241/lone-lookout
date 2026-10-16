/*
 * daycycle.c -- interpolate the colour script and place the sun and moon.
 */
#include "game/daycycle.h"
#include "palette_data.h"

dayclock DAY;

void daycycle_init(float start_hour)
{
	DAY.hour = start_hour;
	DAY.rate = 1.0f;
}

void daycycle_update(float dt)
{
	DAY.hour += dt * DAY.rate / DAY_SECONDS_PER_HOUR;
	while (DAY.hour >= 24.0f) DAY.hour -= 24.0f;
}

/*
 * Keyframe sequence over a 24 h loop starting at dawn. Night holds from
 * 22:30 until 04:30, then eases back toward dawn at 06:00.
 */
static void eval_keys(float t, const ll_palkey **a, const ll_palkey **b, float *f)
{
	static const float times[8] = { 6.0f, 9.0f, 13.0f, 18.5f, 20.0f, 22.5f, 28.5f, 30.0f };
	static const int index[8] = { 0, 1, 2, 3, 4, 5, 5, 0 };
	if (t < 6.0f) t += 24.0f;
	for (int i = 0; i < 7; i++) {
		if (t >= times[i] && t < times[i + 1]) {
			*a = &PAL_KEYS[index[i]];
			*b = &PAL_KEYS[index[i + 1]];
			float u = (t - times[i]) / (times[i + 1] - times[i]);
			*f = u * u * (3.0f - 2.0f * u);   /* ease between keys */
			return;
		}
	}
	*a = *b = &PAL_KEYS[0];
	*f = 0.0f;
}

static rgbf mix3(const float *x, const float *y, float f)
{
	return rgb_lerp(rgb_from(x), rgb_from(y), f);
}

static v3 dir_from(float azimuth_deg, float elev_deg)
{
	/* azimuth 0 = east (+x), 90 = south (+z), 180 = west (-x) */
	float az = DEG2RAD(azimuth_deg), el = DEG2RAD(elev_deg);
	return v3_make(cosf(az) * cosf(el), sinf(el), sinf(az) * cosf(el));
}

void daycycle_eval(daylight *d)
{
	const ll_palkey *a, *b;
	float f;
	float h = DAY.hour;
	eval_keys(h, &a, &b, &f);

	d->hour = h;
	d->sky_top = mix3(a->sky_top, b->sky_top, f);
	d->sky_horizon = mix3(a->sky_horizon, b->sky_horizon, f);
	d->fog = mix3(a->fog, b->fog, f);
	d->sun = mix3(a->sun, b->sun, f);
	d->shadow = mix3(a->shadow, b->shadow, f);
	d->ambient = mix3(a->ambient, b->ambient, f);
	d->key_strength = lerpf(a->key_strength, b->key_strength, f);
	d->fog_density = lerpf(a->fog_density, b->fog_density, f);
	d->window_glow = lerpf(a->window_glow, b->window_glow, f);

	/* The sun crosses from east (05:45) through the south to west (20:15).
	 * Its elevation comes from the colour script so light and colour agree
	 * (e.g. golden hour is authored at 8 degrees). */
	float day_t = (h - 5.75f) / (20.25f - 5.75f);           /* 0..1 over daylight */
	float az = 180.0f * clampf(day_t, -0.2f, 1.2f);
	float el = lerpf(a->sun_elev, b->sun_elev, f);
	d->sun_dir = dir_from(az, el);
	d->sun_vis = smoothstepf(-2.0f, 1.5f, el);

	/* A fixed late-summer moon in the south-east sky at night. */
	d->moon_dir = v3_norm(dir_from(115.0f, 34.0f));
	d->night = 1.0f - smoothstepf(-9.0f, -1.0f, el);

	float sun_weight = smoothstepf(-6.0f, 3.0f, el);
	d->key_dir = v3_norm(v3_lerp(d->moon_dir, d->sun_dir, sun_weight));
	/* Keep the key light from grazing exactly along the ground. */
	if (d->key_dir.y < 0.08f) {
		d->key_dir.y = 0.08f;
		d->key_dir = v3_norm(d->key_dir);
	}
}
