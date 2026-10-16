/*
 * menu.c -- the title screen, the pause menu and the Options screen.
 *
 * Every screen is a short list on a paper card: D-pad up/down or the pointer
 * to pick, A to choose, left/right to change a setting, B to go back.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <wiiuse/wpad.h>
#include "game/menu.h"
#include "game/game.h"
#include "game/save.h"
#include "game/radio.h"
#include "game/director.h"
#include "audio/audio.h"
#include "input/input.h"
#include "gfx/gfx.h"
#include "gfx/text.h"
#include "gfx/foliage.h"
#include "gfx/water.h"
#include "world/sites.h"
#include "palette_data.h"

app_state APP = APP_PLAY;

typedef enum { M_NONE, M_TITLE, M_PAUSE, M_OPTIONS, M_CONFIRM_NEW } menu_id;

static struct {
	menu_id id, back;            /* back: where Options returns to */
	int sel;
	float t;                     /* seconds on this screen */
	bool quit;
	float yaw0;                  /* the title camera's resting heading */
	float clock;                 /* title screen time, for the camera drift */
} M;

#define MAX_ITEMS 20
typedef struct {
	const char *label;
	char value[40];
	int action;
} item_t;

/* Actions. */
enum {
	A_CONTINUE, A_NEW, A_OPTIONS, A_QUIT, A_RESUME, A_SAVE_TITLE, A_SAVE_QUIT, A_YES_NEW, A_NO,
	A_SENS, A_COMFORT, A_INVERT, A_SNAP, A_SWAY, A_BOB, A_COMPASS, A_SUBS, A_BACKING, A_HEARING, A_MONO,
	A_VOL_MUSIC, A_VOL_WORLD, A_VOL_RADIO, A_LOCK30, A_BACK,
};

bool menu_active(void) { return M.id != M_NONE; }
int menu_screen(void) { return (int)M.id; }
bool menu_quit_requested(void) { return M.quit; }

static void go(menu_id id)
{
	M.id = id;
	M.sel = 0;
	M.t = 0.0f;
}

/* ------------------------------------------------------------------------ */
/* The title scene                                                          */
/* ------------------------------------------------------------------------ */

void title_enter(void)
{
	audio_voice_stop();
	audio_radio_hiss(false);
	game_init();
	APP = APP_TITLE;
	S.day = 1;
	S.season_dirty = true;
	/* Augie's bench on Cinder Ridge at golden hour, looking back at the tower. */
	int n, seat = world_find_interactable(IA_SIT, 4);
	const interactable_t *ia = world_interactables(&n);
	P.mode = MODE_SIT;
	P.sit_id = seat;
	P.sit_blend = 1.0f;
	P.pos = v3_make(ia[seat].sit_eye.x, world_ground(ia[seat].sit_eye.x, ia[seat].sit_eye.z, 1e9f, NULL),
	                ia[seat].sit_eye.z);
	v3 eye = ia[seat].sit_eye;
	M.yaw0 = atan2f(TOWER_X - eye.x, -(TOWER_Z - eye.z));
	P.yaw = M.yaw0;
	P.pitch = DEG2RAD(-1.5f);
	P.input_locked = true;
	P.day_card = 100.0f;
	S.area_card_t = 100.0f;
	DAY.hour = 18.3f;
	DAY.rate = 1.0f;
	weather_update(S.day, DAY.hour, 100.0f);
	M.clock = 0.0f;
	go(M_TITLE);
	audio_music_cue("channel");            /* anything playing fades out first */
}

void title_update(float dt)
{
	M.clock += dt;
	/* The sun goes down slowly; the light loops round golden hour. */
	daycycle_update(dt);
	if (DAY.hour > 19.7f) DAY.hour = 18.2f;
	weather_update(S.day, DAY.hour, dt);
	foliage_update(dt);
	water_update(dt);
	P.yaw = M.yaw0 + DEG2RAD(4.0f) * sinf(M.clock * 0.05f);
	P.pitch = DEG2RAD(-1.5f + 1.0f * sinf(M.clock * 0.037f));
	/* A new season's theme, again, once it has rung out. */
	if (!audio_music_playing() && fmodf(M.clock, 40.0f) < dt) audio_music_cue("channel");
}

static void start_play(void)
{
	APP = APP_PLAY;
	P.input_locked = false;
	go(M_NONE);
	director_skip_opening();            /* the theme is already playing */
}

static void new_season(void)
{
	save_forget_game();
	game_init();
	DAY.hour = 15.5f;                   /* Day 1: arrive at the trailhead mid-afternoon */
	S.season_dirty = true;
	start_play();
}

/* ------------------------------------------------------------------------ */
/* Items                                                                    */
/* ------------------------------------------------------------------------ */

static const char *on_off(bool b) { return b ? "On" : "Off"; }

static int build(item_t *it)
{
	int n = 0;
#define ADD(l, a) (it[n].label = (l), it[n].value[0] = 0, it[n].action = (a), &it[n++])
	save_summary sv = save_status();
	switch (M.id) {
	case M_TITLE:
		if (sv.has_game) {
			item_t *c = ADD("Continue", A_CONTINUE);
			int h = (int)sv.hour % 24, m = (int)((sv.hour - (int)sv.hour) * 60.0f);
			snprintf(c->value, sizeof(c->value), "Day %d, %02d:%02d", sv.day, h, m);
		}
		ADD("New season", A_NEW);
		ADD("Options", A_OPTIONS);
		ADD("Back to the Homebrew Channel", A_QUIT);
		break;
	case M_PAUSE:
		ADD("Resume", A_RESUME);
		ADD("Options", A_OPTIONS);
		ADD(sv.card ? "Save and return to the title" : "Return to the title", A_SAVE_TITLE);
		ADD(sv.card ? "Save and quit" : "Quit", A_SAVE_QUIT);
		break;
	case M_CONFIRM_NEW:
		ADD("Start a new season", A_YES_NEW);
		ADD("Keep the saved one", A_NO);
		break;
	case M_OPTIONS: {
		item_t *x;
		x = ADD("Look sensitivity", A_SENS);
		snprintf(x->value, sizeof(x->value), "%.1f", OPT.look_sens);
		x = ADD("Comfort box", A_COMFORT);
		snprintf(x->value, sizeof(x->value), "%s", (const char *[]){ "Small", "Medium", "Large" }[OPT.comfort % 3]);
		x = ADD("Invert look", A_INVERT);
		snprintf(x->value, sizeof(x->value), "%s", on_off(OPT.invert_pitch));
		x = ADD("Snap turn (Nunchuk)", A_SNAP);
		snprintf(x->value, sizeof(x->value), "%s", on_off(OPT.snap_turn));
		x = ADD("Head sway", A_SWAY);
		snprintf(x->value, sizeof(x->value), "%s", on_off(OPT.head_sway));
		x = ADD("Walk bob", A_BOB);
		snprintf(x->value, sizeof(x->value), "%s", on_off(OPT.walk_bob));
		x = ADD("Compass", A_COMPASS);
		snprintf(x->value, sizeof(x->value), "%s", on_off(OPT.compass));
		x = ADD("Subtitles", A_SUBS);
		snprintf(x->value, sizeof(x->value), "%s", OPT.subs_large ? "Large" : "Normal");
		x = ADD("Subtitle backing", A_BACKING);
		snprintf(x->value, sizeof(x->value), "%s", on_off(OPT.subs_backing));
		x = ADD("Hearing effect", A_HEARING);
		snprintf(x->value, sizeof(x->value), "%s", on_off(OPT.hearing));
		x = ADD("Mono audio", A_MONO);
		snprintf(x->value, sizeof(x->value), "%s", on_off(OPT.mono));
		x = ADD("Music", A_VOL_MUSIC);
		snprintf(x->value, sizeof(x->value), "%d", OPT.vol_music);
		x = ADD("Sounds of the world", A_VOL_WORLD);
		snprintf(x->value, sizeof(x->value), "%d", OPT.vol_ambience);
		x = ADD("Radio", A_VOL_RADIO);
		snprintf(x->value, sizeof(x->value), "%d", OPT.vol_voice);
		x = ADD("Lock to 30 fps", A_LOCK30);
		snprintf(x->value, sizeof(x->value), "%s", on_off(OPT.lock30));
		ADD("Back", A_BACK);
		break;
	}
	default:
		break;
	}
#undef ADD
	return n;
}

/* Left (-1), right (+1) or A (+1, wrapping) on a setting. */
static void change(int action, int dir, bool wrap)
{
	int v;
	switch (action) {
	case A_SENS: {
		float s2 = roundf((OPT.look_sens + 0.1f * dir) * 10.0f) / 10.0f;
		if (wrap && s2 > 2.0f + 1e-3f) s2 = 0.5f;
		OPT.look_sens = clampf(s2, 0.5f, 2.0f);
		break;
	}
	case A_COMFORT: OPT.comfort = (u8)((OPT.comfort + 3 + dir) % 3); break;
	case A_INVERT: OPT.invert_pitch = !OPT.invert_pitch; break;
	case A_SNAP: OPT.snap_turn = !OPT.snap_turn; break;
	case A_SWAY: OPT.head_sway = !OPT.head_sway; break;
	case A_BOB: OPT.walk_bob = !OPT.walk_bob; break;
	case A_COMPASS: OPT.compass = !OPT.compass; break;
	case A_SUBS: OPT.subs_large = !OPT.subs_large; break;
	case A_BACKING: OPT.subs_backing = !OPT.subs_backing; break;
	case A_HEARING: OPT.hearing = !OPT.hearing; break;
	case A_MONO: OPT.mono = !OPT.mono; break;
	case A_LOCK30: OPT.lock30 = !OPT.lock30; break;
	case A_VOL_MUSIC:
	case A_VOL_WORLD:
	case A_VOL_RADIO: {
		u8 *p = action == A_VOL_MUSIC ? &OPT.vol_music : (action == A_VOL_WORLD ? &OPT.vol_ambience : &OPT.vol_voice);
		v = *p + dir;
		if (wrap && v > 10) v = 0;
		*p = (u8)(v < 0 ? 0 : (v > 10 ? 10 : v));
		break;
	}
	default: return;
	}
	options_apply();
	audio_ui(UI_TICK);
}

static void activate(int action)
{
	audio_ui(UI_PAGE);
	switch (action) {
	case A_CONTINUE:
		if (save_continue()) start_play();
		break;
	case A_NEW:
		if (save_status().has_game) go(M_CONFIRM_NEW);
		else new_season();
		break;
	case A_YES_NEW: new_season(); break;
	case A_NO: go(M_TITLE); break;
	case A_OPTIONS:
		M.back = M.id;
		go(M_OPTIONS);
		break;
	case A_QUIT: M.quit = true; break;
	case A_RESUME: go(M_NONE); break;
	case A_SAVE_TITLE:
		save_game();
		title_enter();
		break;
	case A_SAVE_QUIT:
		save_game();
		M.quit = true;
		break;
	case A_BACK:
		save_settings();
		go(M.back);
		break;
	default: change(action, +1, true); break;
	}
}

/* ------------------------------------------------------------------------ */
/* Layout and input                                                         */
/* ------------------------------------------------------------------------ */

typedef struct { float x, y, w, row; font_id f; } layout_t;

static layout_t layout(int n)
{
	layout_t L;
	switch (M.id) {
	case M_OPTIONS:
		L.f = FONT_S;
		L.row = 22.5f;
		L.w = 470.0f;
		L.x = (G.ui_w - L.w) * 0.5f;
		L.y = 66.0f;
		break;
	case M_TITLE:
		L.f = FONT_L;
		L.row = 37.0f;
		L.w = 520.0f;
		L.x = 56.0f;
		L.y = G.ui_h - 60.0f - n * L.row;
		break;
	default:
		L.f = FONT_L;
		L.row = 38.0f;
		L.w = 440.0f;
		L.x = (G.ui_w - L.w) * 0.5f;
		L.y = (G.ui_h - n * L.row) * 0.5f + 16.0f;
		break;
	}
	return L;
}

void menu_close(void) { go(M_NONE); }

void menu_pause(void)
{
	if (APP != APP_PLAY || M.id != M_NONE) return;
	audio_ui(UI_PAGE);
	go(M_PAUSE);
}

void menu_update(float dt)
{
	if (M.id == M_NONE) return;
	M.t += dt;
	item_t it[MAX_ITEMS];
	int n = build(it);
	if (n == 0) return;
	if (M.sel >= n) M.sel = n - 1;
	if (M.t < 0.15f) return;                          /* the press that opened it */

	if (in_take(WPAD_BUTTON_UP)) {
		M.sel = (M.sel + n - 1) % n;
		audio_ui(UI_TICK);
	}
	if (in_take(WPAD_BUTTON_DOWN)) {
		M.sel = (M.sel + 1) % n;
		audio_ui(UI_TICK);
	}
	/* The pointer picks the row it's over. */
	if (IN.ir_valid) {
		layout_t L = layout(n);
		float px = (IN.ir_x * 0.5f + 0.5f) * G.ui_w, py = (IN.ir_y * 0.5f + 0.5f) * G.ui_h;
		static float last_px, last_py;
		bool moved = fabsf(px - last_px) + fabsf(py - last_py) > 2.0f;
		last_px = px;
		last_py = py;
		if (moved && px > L.x - 20 && px < L.x + L.w + 20) {
			int row = (int)floorf((py - (L.y - L.row * 0.75f)) / L.row);
			if (row >= 0 && row < n && row != M.sel) {
				M.sel = row;
				audio_ui(UI_TICK);
			}
		}
	}
	if (in_take(WPAD_BUTTON_LEFT)) change(it[M.sel].action, -1, false);
	if (in_take(WPAD_BUTTON_RIGHT)) change(it[M.sel].action, +1, false);
	bool home = in_take(WPAD_BUTTON_HOME);
	if (in_take(WPAD_BUTTON_A)) activate(it[M.sel].action);
	else if (in_take(WPAD_BUTTON_B) || in_take(WPAD_BUTTON_PLUS) || home) {
		switch (M.id) {
		case M_TITLE:
			if (home) M.quit = true;      /* HOME on the title: back to the Homebrew Channel */
			break;
		case M_PAUSE: go(M_NONE); audio_ui(UI_PAGE); break;
		case M_OPTIONS: activate(A_BACK); break;
		case M_CONFIRM_NEW: go(M_TITLE); audio_ui(UI_PAGE); break;
		default: break;
		}
	}
}

/* ------------------------------------------------------------------------ */
/* Drawing                                                                  */
/* ------------------------------------------------------------------------ */

static void centred(font_id f, float y, const char *s, GXColor c)
{
	text_draw_soft(f, (G.ui_w - text_width(f, s, -1)) * 0.5f, y, s, -1, c, (float)c.a / 255.0f);
}

void menu_draw(void)
{
	if (M.id == M_NONE) return;
	item_t it[MAX_ITEMS];
	int n = build(it);
	float a = saturatef(M.t / 0.35f);
	if (M.id == M_TITLE) a = saturatef(M.t / 1.2f);
	layout_t L = layout(n);
	GXColor paper = gx_col(rgb_from(UI_SUBTITLE), a);
	rgbf pc = rgb_from(UI_PAPER);
	GXColor ink = gx_col(rgb_from(UI_INK), 0.92f * a), faint = gx_col(rgb_from(UI_INK), 0.45f * a);
	GXColor pencil = gx_col(rgb_make(0.55f, 0.18f, 0.14f), 0.9f * a);
	mat_vcolor();
	gfx_blend_alpha();

	if (M.id == M_TITLE) {
		/* The name over the sky, the menu on a slip of paper at the bottom left. */
		text_begin();
		centred(FONT_L, 96.0f, "Lone Lookout", paper);
		GXColor sub = paper;
		sub.a = (u8)(sub.a * 0.85f);
		centred(FONT_S, 126.0f, "Kestrel Lookout, the Ashen Peaks. The last weeks of the season.", sub);
		mat_vcolor();
		ui_rect(L.x - 24, L.y - L.row - 8, L.w + 48, n * L.row + 30, gx_col(rgb_scale(pc, 0.98f), 0.9f * a));
		save_summary sv = save_status();
		if (!sv.card) {
			text_begin();
			GXColor nc = paper;
			nc.a = (u8)(nc.a * 0.8f);
			text_draw_soft(FONT_S, G.ui_w - 40.0f - text_width(FONT_S, "No SD card: the season won't be saved.", -1),
			               G.ui_h - 30.0f, "No SD card: the season won't be saved.", -1, nc, 0.8f * a);
		}
	} else {
		/* Pause and Options: the world dims behind a paper card. */
		ui_rect(0, 0, G.ui_w, G.ui_h, (GXColor){ 10, 10, 14, (u8)(120 * a) });
		float top = L.y - L.row - (M.id == M_OPTIONS ? 34.0f : 50.0f);
		float h = n * L.row + (M.id == M_OPTIONS ? 88.0f : 76.0f);
		ui_rect(L.x - 30 + 5, top + 6, L.w + 60, h, (GXColor){ 0, 0, 0, (u8)(60 * a) });
		ui_rect_grad(L.x - 30, top, L.w + 60, h, gx_col(pc, a), gx_col(rgb_scale(pc, 0.93f), a));
		text_begin();
		const char *head = M.id == M_PAUSE ? "Paused" : (M.id == M_OPTIONS ? "Options" : "A new season?");
		text_draw(FONT_L, L.x, top + 38.0f, head, -1, ink);
		if (M.id == M_PAUSE) {
			char when[48];
			int hh = (int)DAY.hour % 24, mm = (int)((DAY.hour - (int)DAY.hour) * 60.0f);
			snprintf(when, sizeof(when), "Day %d, %s, %02d:%02d", S.day, day_info(S.day)->date, hh, mm);
			text_draw(FONT_S, L.x + L.w - text_width(FONT_S, when, -1), top + 36.0f, when, -1, faint);
		}
		if (M.id == M_CONFIRM_NEW) {
			const char *warn = "The saved season is replaced the first time you sleep.";
			text_draw(FONT_S, L.x, top + 62.0f, warn, -1, faint);
		}
	}

	/* The rows. */
	for (int i = 0; i < n; i++) {
		float y = L.y + i * L.row;
		bool on = i == M.sel;
		GXColor c = on ? ink : gx_col(rgb_from(UI_INK), 0.7f * a);
		if (on) {
			mat_vcolor();
			ui_rect(L.x - 14, y - L.row * 0.72f, L.w + 28, L.row - 2, gx_col(rgb_make(0.85f, 0.80f, 0.66f), 0.6f * a));
			ui_rect(L.x - 10, y - L.row * 0.42f, 5, 5, pencil);
		}
		text_begin();
		text_draw(L.f, L.x + 6, y, it[i].label, -1, c);
		if (it[i].value[0]) {
			font_id vf = FONT_S;
			float vw = text_width(vf, it[i].value, -1);
			bool setting = it[i].action >= A_SENS && it[i].action <= A_LOCK30;
			float vx = L.x + L.w - vw - (setting ? 18.0f : 0.0f);
			text_draw(vf, vx, y, it[i].value, -1, on ? ink : faint);
			if (setting && on) {
				text_draw(FONT_S, vx - 16.0f, y, "<", -1, pencil);
				text_draw(FONT_S, L.x + L.w - 8.0f, y, ">", -1, pencil);
			}
		}
	}

	if (M.id == M_OPTIONS) {
		text_begin();
		const char *hint = "Left / right to change     A to choose     B back";
		text_draw(FONT_S, L.x, L.y + n * L.row + 8.0f, hint, -1, faint);
	}
}
