/*
 * input.c -- WPAD polling, IR smoothing and stick shaping.
 */
#include <math.h>
#include <wiiuse/wpad.h>
#include "input/input.h"
#include "core/mathx.h"

input_t IN;

void input_init(void)
{
	WPAD_Init();
	WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);
	WPAD_SetVRes(WPAD_CHAN_0, 640, 480);
	WPAD_SetIdleTimeout(300);
}

#ifdef LL_INPUT_SCRIPT
#include "game/radio.h"
/*
 * Scripted input for automated testing in Dolphin (make WALKTEST=1): drives
 * the real look/walk/radio code paths with synthetic Remote + Nunchuk data.
 * Walking and turning are timed; radio presses react to the radio's state so
 * the script doesn't depend on how fast the emulator runs.
 */
#ifdef LL_SOAK_RIDGE
#include "game/game.h"
#include "game/story.h"
#include "game/dialogue.h"
#include "world/world.h"
#include "world/sites.h"

/* Sit on Augie's bench, facing the sunset. */
static void sit_on_bench(void)
{
	int n, seat = world_find_interactable(IA_SIT, 4);
	const interactable_t *ia = world_interactables(&n);
	P.mode = MODE_SIT;
	P.sit_id = seat;
	P.sit_blend = 1.0f;
	P.pos = v3_make(ia[seat].sit_eye.x, world_ground(ia[seat].sit_eye.x, ia[seat].sit_eye.z, 1e9f, NULL),
	                ia[seat].sit_eye.z);
	P.stand_pos = P.pos;
	P.yaw = BENCH_FACING;
	P.pitch = DEG2RAD(-3.0f);
	P.day_card = 100.0f;
}

/*
 * The ridge time-lapse soak (make SOAKRIDGE=1): seated on the bench holding
 * Z, so the hours run forty times as fast; the radio's calls are answered
 * and played through (they pause the time-lapse). At 21:30 the next day
 * starts (Day 11 wraps to a fresh season), so an hour covers dozens of days:
 * every palette, the weather, the seasonal re-recording and the music.
 */
static void ridge_soak(u32 f)
{
	static u32 ring_frames, choice_frames;
	u32 prev = IN.held;
	IN.connected = true;
	IN.nunchuk = true;
	IN.held = 0;
	IN.stick_x = IN.stick_y = 0.0f;
	IN.ir_valid = true;
	IN.ir_x = IN.ir_y = 0.0f;
	if (f == 1 || (P.mode != MODE_SIT && !radio_busy())) sit_on_bench();
	if (DAY.hour > 21.5f && !radio_busy()) {
		int next = S.day % NUM_DAYS + 1;
		if (next == 1) {
			radio_init();
			dlg_init();
			story_init();
		}
		game_new_day(next);
		sit_on_bench();
	}
	IN.held |= WPAD_NUNCHUK_BUTTON_Z;                         /* let time pass */
	bool ringing = radio_busy() && !radio_in_call();
	ring_frames = ringing ? ring_frames + 1 : 0;
	choice_frames = radio_wants_pointer() ? choice_frames + 1 : 0;
	if (ring_frames == 60) IN.held |= WPAD_BUTTON_B;          /* answer */
	if (choice_frames == 45) IN.held |= WPAD_BUTTON_A;        /* take the first card */
	if (radio_in_call() && !radio_wants_pointer() && (f % 240) == 0) IN.held |= WPAD_BUTTON_A;
	IN.down |= IN.held & ~prev;
	IN.up |= prev & ~IN.held;
}
#endif

static void scripted(u32 f)
{
#ifdef LL_SOAK_RIDGE
	ridge_soak(f);
	return;
#endif
	static u32 ring_frames, choice_frames, idle_after_call, book;
	static int stage;   /* 0 waiting for ring, 1 in call, 2 call done, 3 check-in sent, 4 notebook, 5 done */
	u32 prev = IN.held;
	IN.connected = true;
	IN.held = 0;
	IN.stick_x = IN.stick_y = 0.0f;
	IN.ir_valid = true;
	IN.ir_x = IN.ir_y = 0.0f;
#ifdef LL_SCRIPT_REMOTE_ONLY
	/* make REMOTETEST=1: the same test with no Nunchuk (D-pad walks, 2 is the notebook). */
	IN.nunchuk = false;
	const u32 NOTEBOOK = WPAD_BUTTON_2;
	if (f >= 240 && f < 600) IN.held |= WPAD_BUTTON_UP;        /* walk forward */
#else
	IN.nunchuk = true;
	const u32 NOTEBOOK = WPAD_BUTTON_DOWN;
	if (f >= 240 && f < 600) IN.stick_y = 1.0f;                 /* walk forward */
#endif
	if (f >= 600 && f < 660) IN.ir_x = 1.0f;                   /* turn right */
	if (f >= 660 && f < 720) IN.ir_x = -1.0f;                  /* and back */

	bool ringing = radio_busy() && !radio_in_call();
	ring_frames = ringing ? ring_frames + 1 : 0;
	choice_frames = radio_wants_pointer() ? choice_frames + 1 : 0;
	if (stage == 0 && ring_frames == 60) { IN.held |= WPAD_BUTTON_B; stage = 1; }   /* answer */
	if (choice_frames == 45) IN.held |= WPAD_BUTTON_DOWN;                           /* 2nd card */
	if (choice_frames == 90) IN.held |= WPAD_BUTTON_A;                              /* choose it */
	if (stage == 1 && !radio_busy()) stage = 2;
	idle_after_call = (stage == 2) ? idle_after_call + 1 : 0;
	if (stage == 2 && idle_after_call == 90) { IN.held |= WPAD_BUTTON_B; stage = 3; } /* call Basin */
	if (stage == 3 && !radio_busy()) { stage = 4; book = 0; }
	if (stage == 4) {                                         /* notebook: open, two pages on, close */
		book++;
		if (book == 60) IN.held |= NOTEBOOK;
		if (book == 120 || book == 180) IN.held |= WPAD_BUTTON_RIGHT;
		if (book == 300) { IN.held |= WPAD_BUTTON_B; stage = 5; }
	}
#ifdef LL_SOAK
	/* The soak keeps everything busy for an hour: a stroll every half
	 * minute, Listen every forty seconds, a call to Basin every minute. */
	if (stage == 5) {
		u32 t = f % 3600;
		if (t >= 300 && t < 600) IN.stick_y = 0.8f;
		if (t >= 600 && t < 700) IN.ir_x = 0.9f;
		if (t >= 1800 && t < 2100) IN.stick_y = -0.6f;
		if ((f % 2400) < 600) IN.held |= WPAD_NUNCHUK_BUTTON_C;
		if (t == 3000 && !radio_busy()) IN.held |= WPAD_BUTTON_B;
		if (radio_in_call() && !radio_wants_pointer() && (f % 300) == 0) IN.held |= WPAD_BUTTON_A;
		if (ringing && ring_frames == 120) IN.held |= WPAD_BUTTON_B;
	}
#endif
	IN.down |= IN.held & ~prev;
	IN.up |= prev & ~IN.held;
}
#endif

#ifdef LL_MENU_TEST
#include "game/menu.h"
#include "game/game.h"
/*
 * The scripted menu and save test (make MENUTEST=1): from the title, take the
 * first item (New season, or Continue if a season is saved), walk, pause,
 * turn the compass on in Options, save and return to the title, Continue,
 * then Save and quit. Each step waits for the screen it needs.
 */
typedef struct { int screen; u16 wait; u32 buttons; u16 hold_stick; } mstep;
#define PLAYING (-1)
static const mstep MSTEPS[] = {
	{ MENU_TITLE, 150, WPAD_BUTTON_A, 0 },
	{ PLAYING, 150, 0, 240 },
	{ PLAYING, 30, WPAD_BUTTON_HOME, 0 },
	{ MENU_PAUSE, 60, WPAD_BUTTON_DOWN, 0 },
	{ MENU_PAUSE, 20, WPAD_BUTTON_A, 0 },
	{ MENU_OPTIONS, 60, WPAD_BUTTON_DOWN, 0 }, { MENU_OPTIONS, 15, WPAD_BUTTON_DOWN, 0 },
	{ MENU_OPTIONS, 15, WPAD_BUTTON_DOWN, 0 }, { MENU_OPTIONS, 15, WPAD_BUTTON_DOWN, 0 },
	{ MENU_OPTIONS, 15, WPAD_BUTTON_DOWN, 0 }, { MENU_OPTIONS, 15, WPAD_BUTTON_DOWN, 0 },
	{ MENU_OPTIONS, 40, WPAD_BUTTON_A, 0 },                   /* Compass: on */
	{ MENU_OPTIONS, 60, WPAD_BUTTON_B, 0 },                   /* back (settings saved) */
	{ MENU_PAUSE, 60, WPAD_BUTTON_DOWN, 0 }, { MENU_PAUSE, 15, WPAD_BUTTON_DOWN, 0 },
	{ MENU_PAUSE, 40, WPAD_BUTTON_A, 0 },                     /* Save and return to the title */
	{ MENU_TITLE, 180, WPAD_BUTTON_A, 0 },                    /* Continue */
	{ PLAYING, 240, WPAD_BUTTON_HOME, 0 },
	{ MENU_PAUSE, 60, WPAD_BUTTON_DOWN, 0 }, { MENU_PAUSE, 15, WPAD_BUTTON_DOWN, 0 },
	{ MENU_PAUSE, 15, WPAD_BUTTON_DOWN, 0 },
	{ MENU_PAUSE, 60, WPAD_BUTTON_A, 0 },                     /* Save and quit */
};

static void menu_script(void)
{
	static int step, waited, stick;
	u32 prev = IN.held;
	IN.connected = true;
	IN.nunchuk = true;
	IN.held = 0;
	IN.stick_x = IN.stick_y = 0.0f;
	IN.ir_valid = false;
	if (stick > 0) {
		stick--;
		IN.stick_y = 1.0f;
	} else if (step < (int)(sizeof(MSTEPS) / sizeof(MSTEPS[0]))) {
		const mstep *s = &MSTEPS[step];
		int now = APP == APP_PLAY && !menu_active() ? PLAYING : menu_screen();
		if (now == s->screen) {
			if (++waited >= s->wait) {
				IN.held |= s->buttons;
				stick = s->hold_stick;
				waited = 0;
				step++;
			}
		} else {
			waited = 0;
		}
	}
	IN.down |= IN.held & ~prev;
	IN.up |= prev & ~IN.held;
}
#endif

void input_poll(void)
{
#ifdef LL_INPUT_SCRIPT
	static u32 frame;
	WPAD_ScanPads();
	scripted(frame++);
	return;
#endif
#ifdef LL_MENU_TEST
	WPAD_ScanPads();
	menu_script();
	return;
#endif
	WPAD_ScanPads();
	WPADData *w = WPAD_Data(WPAD_CHAN_0);
	IN.connected = w && w->err == WPAD_ERR_NONE;
	if (!IN.connected) {
		IN.held = IN.down = IN.up = 0;
		IN.stick_x = IN.stick_y = 0.0f;
		IN.ir_valid = false;
		return;
	}
	IN.held = WPAD_ButtonsHeld(WPAD_CHAN_0);
	/* Presses accumulate until the simulation consumes them (input_consume),
	 * so a frame that runs zero sim steps never drops one. */
	IN.down |= WPAD_ButtonsDown(WPAD_CHAN_0);
	IN.up |= w->btns_u;

	IN.nunchuk = w->exp.type == WPAD_EXP_NUNCHUK;
	IN.stick_x = IN.stick_y = 0.0f;
	if (IN.nunchuk) {
		const joystick_t *js = &w->exp.nunchuk.js;
		float mag = clampf(js->mag, 0.0f, 1.0f);
		if (mag != mag) mag = 0.0f;                      /* NaN guard */
		/* Radial dead zone, then rescale so movement starts gently. */
		const float dz = 0.14f;
		if (mag > dz) {
			float m = (mag - dz) / (1.0f - dz);
			m = m * m * (3.0f - 2.0f * m) * 0.35f + m * 0.65f;
			float a = DEG2RAD(js->ang);
			IN.stick_x = sinf(a) * m;
			IN.stick_y = cosf(a) * m;
		}
	}

	/* IR: ease toward the new position (~60 ms) to hide sensor-bar jitter. */
	bool valid = w->ir.valid != 0;
	if (valid) {
		float nx = clampf(w->ir.x / 320.0f - 1.0f, -1.2f, 1.2f);
		float ny = clampf(w->ir.y / 240.0f - 1.0f, -1.2f, 1.2f);
		if (!IN.ir_valid) {
			IN.ir_x = nx;
			IN.ir_y = ny;
		} else {
			IN.ir_x += (nx - IN.ir_x) * 0.45f;
			IN.ir_y += (ny - IN.ir_y) * 0.45f;
		}
	}
	IN.ir_valid = valid;

	float roll = w->orient.roll;
	if (roll == roll && fabsf(roll) < 180.0f)
		IN.roll_deg += (roll - IN.roll_deg) * 0.12f;
}
