/*
 * days.c -- the day table and the weather.
 */
#include <math.h>
#include <stdio.h>
#include "game/days.h"
#include "audio/audio.h"
#include "core/noise.h"

weather_t WX;

static const day_def DAYS[NUM_DAYS] = {
	{ "Channel Four", "21 August", 0.00f, WX_CLEAR },
	{ "Piano Man", "22 August", 0.03f, WX_WARM },
	{ "Say Again", "23 August", 0.06f, WX_HIGH_CLOUD },
	{ "Field Notes", "25 August", 0.15f, WX_BREEZY },
	{ "Dry Lightning", "28 August", 0.30f, WX_STORM },
	{ "Relay", "30 August", 0.40f, WX_SMOKE },
	{ "Land's Too Loud", "3 September", 0.50f, WX_HAZE },
	{ "Bugle", "6 September", 0.65f, WX_BREEZY },
	{ "Last Season", "10 September", 0.80f, WX_COLD },
	{ "First Frost", "13 September", 0.92f, WX_FROST },
	{ "First Rain", "15 September", 1.00f, WX_RAIN },
};

bool day_is_supply(int day) { return day == 3 || day == 9 || day == 10; }

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
	case WX_BREEZY:
		w.cloud = 0.22f + 0.08f * sinf(hour * 0.9f);
		w.wind = 1.05f;
		break;
	case WX_SMOKE:
		/* The Harrow fire, two days on: a thin pall, worst in the afternoon. */
		w.haze = 0.35f + 0.2f * smoothstepf(11.0f, 16.0f, hour);
		w.cloud = 0.1f;
		w.wind = 0.45f;
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
	case WX_FROST:
		/* A white morning: mist in the hollows until the sun gets to it. */
		w.cold = 1.0f;
		w.cloud = 0.3f * (1.0f - smoothstepf(8.0f, 11.0f, hour));
		w.wind = 0.3f;
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

	/* Rain: low and grey, at whatever brightness the hour has (a fixed grey made
	 * rainy nights glow), and the horizon lost in it. */
	d->sky_top = rgb_lerp(d->sky_top, grey_of(d->sky_top, 0.8f), w->rain * 0.6f);
	d->fog = rgb_lerp(d->fog, grey_of(d->fog, 0.85f), w->rain * 0.5f);
	d->sky_horizon = rgb_lerp(d->sky_horizon, d->fog, w->rain * 0.7f);

	/* Haze from far-off fires: orange air, a red sun. */
	float h = w->haze;
	d->fog = rgb_lerp(d->fog, rgb_make(0.80f, 0.58f, 0.40f), h * 0.6f);
	d->sky_horizon = rgb_lerp(d->sky_horizon, rgb_make(0.90f, 0.62f, 0.40f), h * 0.6f);
	d->sky_top = rgb_lerp(d->sky_top, rgb_make(0.62f, 0.58f, 0.56f), h * 0.4f);
	d->sun = rgb_lerp(d->sun, rgb_make(1.0f, 0.52f, 0.30f), h * 0.6f);
	d->fog_density *= 1.0f + 0.9f * h;

	/* In thick air the land fades into the sky: no bright band behind fogged hills. */
	d->sky_horizon = rgb_lerp(d->sky_horizon, d->fog, saturatef((d->fog_density - 0.9f) * 0.8f));

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

/* Readings at the instrument shelter on the catwalk: dry and wet bulb,
 * humidity, wind, and the fuel stick (how dry the forest is). Season and
 * weather drive them, so the numbers drift toward autumn day by day. */
readings_t weather_read(int day, float hour)
{
	const day_def *d = day_info(day);
	float daytime = sinf(clampf((hour - 6.0f) / 15.0f, 0.0f, 1.0f) * LL_PI);   /* peaks mid-afternoon */
	float temp = lerpf(52.0f, 40.0f, d->season) + daytime * lerpf(26.0f, 18.0f, d->season);
	temp -= 10.0f * WX.cold * (1.0f - daytime) + 8.0f * WX.rain + 6.0f * WX.storm;
	float rh = clampf(28.0f + 30.0f * (1.0f - daytime) + 40.0f * WX.rain + 15.0f * WX.cloud + 10.0f * d->season, 8.0f, 99.0f);
	static const char *dirs[] = { "W", "SW", "W", "NW", "SW", "W", "S", "NW", "N", "NW", "SW" };
	readings_t r;
	r.dry = (int)(temp + 0.5f);
	r.wet = (int)(temp - (100.0f - rh) * 0.2f + 0.5f);
	r.rh = (int)(rh + 0.5f);
	r.wind_dir = dirs[(day - 1) % 11];
	r.wind_mph = (int)(4.0f + 14.0f * WX.wind + 10.0f * WX.storm);
	r.fuel = clampf(5.0f + rh * 0.12f + 6.0f * WX.rain, 3.0f, 20.0f);
	return r;
}

const char *weather_readings(int day, float hour)
{
	static char buf[200];
	readings_t r = weather_read(day, hour);
	snprintf(buf, sizeof(buf),
	         "Dry bulb %d\xC2\xB0" "F. Wet bulb %d\xC2\xB0" "F. Humidity %d%%.\nWind %s, %d mph.\nFuel stick %.1f grams.",
	         r.dry, r.wet, r.rh, r.wind_dir, r.wind_mph, r.fuel);
	return buf;
}
