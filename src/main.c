/*
 * Lone Lookout -- a quiet first-person game for the Nintendo Wii.
 *
 * main.c: boot, then the frame loop. The simulation steps at a fixed 60 Hz;
 * the renderer presents at 60 fps, or locks to an even 30 when frames run
 * long (see gfx_end). HOME returns to the Homebrew Channel.
 */
#include <stdlib.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include "core/clock.h"
#include "gfx/gfx.h"
#include "gfx/sky.h"
#include "gfx/terrain.h"
#include "gfx/foliage.h"
#include "gfx/paths.h"
#include "gfx/water.h"
#include "gfx/props.h"
#include "gfx/weather_fx.h"
#include "gfx/creatures.h"
#include "game/game.h"
#include "game/hud.h"
#include "game/tour.h"
#include "game/radio.h"
#include "game/story.h"
#include "input/input.h"
#include "audio/audio.h"
#include "world/world.h"

static volatile bool s_power_off;

static void on_power(void) { s_power_off = true; }
static void on_wpad_power(s32 chan) { s_power_off = true; }

static void draw_scene(const daylight *d)
{
	v3 eye = game_eye();
	gfx_camera(eye, P.yaw, P.pitch, P.roll, game_fov());
	bool indoor = world_indoor(v3_make(eye.x, eye.y - 1.5f, eye.z)) != INDOOR_NONE;

	sky_draw(d, WX.cloud);
	backdrop_draw(d);
	smoke_draw(d);

	gfx_fog(d);
	G.stat_chunks = terrain_draw(d);
	paths_draw(d);
	props_state ps = { S.lamp, S.binoculars, story_flag(FLAG_SHUTTERS_DONE), story_flag(FLAG_CAIRN_STONE) };
	props_draw(d, &ps);
	creatures_draw(d);
	terrain_draw_tufts(d);
	G.stat_trees = foliage_draw(d, WX.wind * 1.6f);
	water_draw(d);
	rain_draw(d, WX.rain, WX.wind, indoor);
	gfx_fog_off();

	gfx_post(d, 0.0f);
	hud_draw();
}

/* A new day's season: re-record the coloured lists while the GPU is idle. */
static void apply_season(void)
{
	GX_DrawDone();
	float s = game_season();
	terrain_set_season(s);
	foliage_set_season(s);
	S.season_dirty = false;
}

int main(int argc, char **argv)
{
	gfx_init();
	input_init();
	SYS_SetPowerCallback(on_power);
	WPAD_SetPowerButtonCallback(on_wpad_power);

	world_init();
	game_init();
	DAY.hour = 15.5f;                      /* Day 1: arrive at the trailhead mid-afternoon */
#ifdef LL_INPUT_SCRIPT
	DAY.hour = 16.97f;                     /* the scripted test: Dot's first call rings at 17:00 */
#endif
	DAY.rate = 1.0f;
	terrain_build(game_season());
	foliage_build(game_season());
	paths_build();
	water_build();
	props_build();
	creatures_build();
	sky_init();
	audio_init();
	S.season_dirty = false;
	clock_init();

	while (!s_power_off) {
		input_poll();
		if (in_held(WPAD_BUTTON_HOME))
			break;

		bool black = tour_frame();
		int steps = clock_steps();
		for (int i = 0; i < steps; i++) {
			game_update(SIM_DT);
			input_consume();
		}
		if (S.season_dirty) apply_season();

		daylight d;
		daycycle_eval(&d);
		weather_apply(&d);
		indoor_t in = world_indoor(P.pos);
		float above = P.pos.y - world_height(P.pos.x, P.pos.z);
		float birds = (1.0f - d.night) * (d.hour > 5.0f && d.hour < 20.5f ? 1.0f : 0.0f) * (1.0f - WX.rain) *
		              (1.0f - 0.7f * WX.storm);
		audio_set_scene(saturatef(WX.wind * (0.8f + 0.3f * (above > 4.0f))), birds, d.night,
		                saturatef((P.pos.y - 60.0f) / 50.0f + (above > 4.0f ? 0.3f : 0.0f)), 0.0f);
		audio_set_weather(WX.rain, in != INDOOR_NONE ? 1.0f : 0.0f);
		ramp_update(&d);

		gfx_begin(black ? rgb_make(0, 0, 0) : d.fog);
		if (!black)
			draw_scene(&d);
		gfx_end();
	}

	if (s_power_off)
		SYS_ResetSystem(SYS_POWEROFF, 0, 0);
	return 0;
}
