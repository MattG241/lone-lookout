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
#include "gfx/props.h"
#include "game/game.h"
#include "game/hud.h"
#include "game/tour.h"
#include "game/radio.h"
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

	sky_draw(d);
	backdrop_draw(d);

	gfx_fog(d);
	terrain_draw(d);
	props_draw(d);
	terrain_draw_tufts(d);
	foliage_draw(d, 1.0f);
	gfx_fog_off();

	gfx_post(d, 0.0f);
	hud_draw();
}

int main(int argc, char **argv)
{
	gfx_init();
	input_init();
	SYS_SetPowerCallback(on_power);
	WPAD_SetPowerButtonCallback(on_wpad_power);

	world_init();
	terrain_build();
	foliage_build();
	props_build();
	sky_init();
	audio_init();

	daycycle_init(17.6f);
	game_init();
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

		daylight d;
		daycycle_eval(&d);
		audio_set_scene(0.55f + 0.25f * (P.pos.y - world_height(P.pos.x, P.pos.z) > 4.0f),
		                (1.0f - d.night) * (d.hour > 5.0f && d.hour < 20.5f ? 1.0f : 0.0f),
		                d.night, saturatef((P.pos.y - world_pad_height()) / 10.0f), 0.0f);
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
