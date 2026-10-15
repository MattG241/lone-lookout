/*
 * days.c -- the day table and the weather.
 */
#include <math.h>
#include "game/days.h"
#include "audio/audio.h"
#include "core/noise.h"

weather_t WX;

static const day_def DAYS[NUM_DAYS] = {
	{ "Channel Four", "21 August", 0.00f, WX_CLEAR },
	{ "Piano Man", "22 August", 0.05f, WX_WARM },
	{ "Say Again", "23 August", 0.10f, WX_HIGH_CLOUD },
	{ "Dry Lightning", "28 August", 0.30f, WX_STORM },
	{ "Land's Too Loud", "3 September", 0.50f, WX_HAZE },
	{ "Last Season", "10 September", 0.80f, WX_COLD },
	{ "First Rain", "15 September", 1.00f, WX_RAIN },
};

const day_def *day_info(int day)
{
	if (day < 1) day = 1;
	if (day > NUM_DAYS) day = NUM_DAYS;
	return &DAYS[day - 1];
}

static rng_t s_rng = { 0x5EED1234u };
static float s_next_flash = 6.0f, s_flash_t = 99.0f, s_thunder_in = -1.0f, s_thunder_gain;

void weather_update(int day, float hour, float dt)
{
	weather_t w = { 0 };
	w.wind = 0.55f;
	w.cloud = 0.08f;
	switch (day_info(day)->weather) {
	case WX_CLEAR: break;
	case WX_WARM:
		w.wind = 0.4f;
		w.cloud = 0.04f;
		break;
	case WX_HIGH_CLOUD:
		w.cloud = 0.45f + 0.1f * sinf(hour * 0.7f);
		w.wind = 0.95f;
		break;
	case WX_STORM:
		/* Dry lightning, 14:00 to 17:30. */
		w.storm = smoothstepf(13.3f, 14.3f, hour) * (1.0f - smoothstepf(17.3f, 18.3f, hour));
		w.cloud = 0.2f + 0.7f * w.storm;
		w.wind = 0.6f + 0.8f * w.storm;
		break;
	case WX_HAZE:
		w.haze = 1.0f;
		w.cloud = 0.05f;
		w.wind = 0.35f;
		break;
	case WX_COLD:
		w.cold = 1.0f;
		w.cloud = 0.12f;
		w.wind = 0.7f;
		break;
	case WX_RAIN:
		/* Clouding over through the morning; rain from noon. */
		w.cloud = 0.2f + 0.75f * smoothstepf(8.5f, 12.0f, hour);
		w.rain = smoothstepf(11.8f, 12.8f, hour);
		w.wind = 0.6f + 0.4f * w.rain;
		break;
	}

	/* Lightning: a double flicker every few seconds at the storm's height,
	 * and thunder after it -- the gap says how far away. */
	s_flash_t += dt;
	if (w.storm > 0.5f) {
		s_next_flash -= dt;
		if (s_next_flash <= 0.0f) {
			s_flash_t = 0.0f;
			s_next_flash = rng_range(&s_rng, 7.0f, 22.0f);
			s_thunder_in = rng_range(&s_rng, 1.5f, 6.0f);
			s_thunder_gain = 1.0f - (s_thunder_in - 1.5f) / 4.5f;
		}
	}
	float f = s_flash_t;
	w.flash = (f < 0.08f ? 1.0f : 0.0f) + (f > 0.16f && f < 0.3f ? 0.7f * (1.0f - (f - 0.16f) / 0.14f) : 0.0f);
	if (s_thunder_in > 0.0f && (s_thunder_in -= dt) <= 0.0f) {
		audio_ui_gain(UI_THUNDER, 0.5f + 0.5f * s_thunder_gain);
		s_thunder_in = -1.0f;
	}

	/* Ease between states so nothing ever snaps (sleeping resets the day anyway). */
	float k = 1.0f - expf(-dt * 0.5f);
	WX.cloud += (w.cloud - WX.cloud) * k;
	WX.haze += (w.haze - WX.haze) * k;
	WX.storm += (w.storm - WX.storm) * k;
	WX.rain += (w.rain - WX.rain) * k;
	WX.wind += (w.wind - WX.wind) * k;
	WX.cold += (w.cold - WX.cold) * k;
	WX.flash = w.flash;
}

static rgbf grey_of(rgbf c, float k)
{
	float l = c.r * 0.3f + c.g * 0.55f + c.b * 0.15f;
	return rgb_scale(rgb_make(l, l, l * 1.03f), k);
}

void weather_apply(daylight *d)
{
	const weather_t *w = &WX;
	/* Cloud: the sun's colour drains and the sky greys over. */
	float c = w->cloud;
	d->key_strength *= 1.0f - 0.6f * c * c;
	d->sun = rgb_lerp(d->sun, grey_of(d->sun, 1.0f), c * 0.7f);
	d->sky_top = rgb_lerp(d->sky_top, grey_of(d->sky_top, 1.0f), c * 0.65f);
	d->sky_horizon = rgb_lerp(d->sky_horizon, grey_of(d->sky_horizon, 0.95f), c * 0.6f);
	d->fog = rgb_lerp(d->fog, grey_of(d->fog, 0.95f), c * 0.55f);
	d->fog_density *= 1.0f + 0.4f * c + 0.9f * w->rain;
	d->ambient = rgb_lerp(d->ambient, grey_of(d->ambient, 1.0f), c * 0.4f);

	/* Storm: darker, bruised light. */
	float s = w->storm;
	d->ambient = rgb_scale(d->ambient, 1.0f - 0.25f * s);
	d->sky_top = rgb_lerp(d->sky_top, rgb_make(0.32f, 0.33f, 0.38f), s * 0.6f);
	d->sky_horizon = rgb_lerp(d->sky_horizon, rgb_make(0.55f, 0.52f, 0.52f), s * 0.5f);
	d->fog = rgb_lerp(d->fog, rgb_make(0.50f, 0.48f, 0.49f), s * 0.5f);

	/* Rain: low and grey. */
	d->sky_top = rgb_lerp(d->sky_top, rgb_make(0.50f, 0.53f, 0.56f), w->rain * 0.5f);
	d->fog = rgb_lerp(d->fog, rgb_make(0.62f, 0.64f, 0.66f), w->rain * 0.35f);

	/* Haze from far-off fires: orange air, a red sun. */
	float h = w->haze;
	d->fog = rgb_lerp(d->fog, rgb_make(0.80f, 0.58f, 0.40f), h * 0.6f);
	d->sky_horizon = rgb_lerp(d->sky_horizon, rgb_make(0.90f, 0.62f, 0.40f), h * 0.6f);
	d->sky_top = rgb_lerp(d->sky_top, rgb_make(0.62f, 0.58f, 0.56f), h * 0.4f);
	d->sun = rgb_lerp(d->sun, rgb_make(1.0f, 0.52f, 0.30f), h * 0.6f);
	d->fog_density *= 1.0f + 0.9f * h;

	/* Cold, clear air: further views, a bluer sky. */
	d->fog_density *= 1.0f - 0.25f * w->cold;
	d->sky_top = rgb_lerp(d->sky_top, rgb_make(0.45f, 0.62f, 0.82f), w->cold * 0.2f * (1.0f - d->night));

	/* Lightning lights everything for a moment. */
	if (w->flash > 0.0f) {
		rgbf fl = rgb_make(0.85f, 0.88f, 1.0f);
		d->ambient = rgb_add(d->ambient, rgb_scale(fl, 0.7f * w->flash));
		d->sky_top = rgb_lerp(d->sky_top, fl, 0.5f * w->flash);
		d->sky_horizon = rgb_lerp(d->sky_horizon, fl, 0.5f * w->flash);
		d->fog = rgb_lerp(d->fog, fl, 0.35f * w->flash);
	}
}
