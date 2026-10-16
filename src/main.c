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
#include "game/listen.h"
#include "input/input.h"
#include "audio/audio.h"
#include "audio/vo.h"
#include "core/storage.h"
#include "game/save.h"
#include "game/menu.h"
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
	backdrop_draw_far(d);
	smoke_draw(d);                         /* 900 m out: between the far and near ranges */
	backdrop_draw_near(d);

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
	storage_init();                        /* the SD card, for the save and recorded voices */
	game_init();
	DAY.hour = 15.5f;                      /* Day 1: arrive at the trailhead mid-afternoon */
#ifdef LL_INPUT_SCRIPT
	DAY.hour = 16.97f;                     /* the scripted test: Dot's first call rings at 17:00 */
#endif
#ifdef LL_SOAK
	game_new_day(NUM_DAYS);                /* the soak: Day 11, in the cab as the rain starts */
	DAY.hour = 12.3f;
	P.day_card = 100.0f;
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
	save_init();                           /* settings, and whether there's a season to continue */
	vo_init();
#if !defined(LL_SHOT_TOUR) && !defined(LL_INPUT_SCRIPT)
	title_enter();
#endif
	S.season_dirty = false;
	clock_init();

	while (!s_power_off) {
		input_poll();
#if defined(LL_SHOT_TOUR) || defined(LL_INPUT_SCRIPT)
		if (in_held(WPAD_BUTTON_HOME))
			break;
#else
		/* HOME or + pauses (time stops); the menus have their own way out. */
		if (APP == APP_PLAY && !menu_active() && (in_down(WPAD_BUTTON_HOME) || in_down(WPAD_BUTTON_PLUS)))
			menu_pause();
		G.force_half_rate = OPT.lock30;
#endif

		bool black = tour_frame();
		int steps = clock_steps();
		for (int i = 0; i < steps; i++) {
			if (menu_active()) menu_update(SIM_DT);
			if (APP == APP_TITLE) title_update(SIM_DT);
			else if (!menu_active()) game_update(SIM_DT);
			input_consume();
		}
		if (menu_quit_requested()) break;
		if (S.season_dirty) apply_season();

		daylight d;
		daycycle_eval(&d);
		weather_apply(&d);
		indoor_t in = world_indoor(P.pos);
		float above = P.pos.y - world_height(P.pos.x, P.pos.z);
		float birds = (1.0f - d.night) * (d.hour > 5.0f && d.hour < 20.5f ? 1.0f : 0.0f) * (1.0f - WX.rain) *
		              (1.0f - 0.7f * WX.storm);
		audio_set_scene(saturatef(WX.wind * (0.8f + 0.3f * (above > 4.0f))), birds, d.night,
		                saturatef((P.pos.y - 60.0f) / 50.0f + (above > 4.0f ? 0.3f : 0.0f)), listen_amount());
		audio_set_weather(WX.rain, in != INDOOR_NONE ? 1.0f : 0.0f);
		ramp_update(&d);

		gfx_begin(black ? rgb_make(0, 0, 0) : d.fog);
#ifdef LL_SOAK
		black = G.frame % 600 != 0;            /* the soak only draws the frames it shows */
#endif
		if (!black)
			draw_scene(&d);
		gfx_end();
	}

	if (s_power_off)
		SYS_ResetSystem(SYS_POWEROFF, 0, 0);
	return 0;
}
