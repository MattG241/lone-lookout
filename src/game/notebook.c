/*
 * notebook.c -- the notebook (D-pad down, or 2 without a Nunchuk).
 *
 * One paper spread with five tabs: Today (the readings and Dot's errand),
 * Map (Augie's, once found under the cot, with a pencil mark where you
 * stand), Notes (Augie's notes as you find them; A reads one again), Radio
 * (everything said on the radio) and Sounds (the Listen log). D-pad
 * left/right turns the page; B closes it.
 */
#include <math.h>
#include <stdio.h>
#include <wiiuse/wpad.h>
#include "game/notebook.h"
#include "game/game.h"
#include "game/radio.h"
#include "game/story.h"
#include "game/listen.h"
#include "audio/audio.h"
#include "input/input.h"
#include "gfx/gfx.h"
#include "gfx/text.h"
#include "palette_data.h"

static const char *TABS[NB_PAGES] = { "Today", "Map", "Notes", "Radio", "Sounds" };

void notebook_update(float dt)
{
	if (P.input_locked) return;
	if (in_take(WPAD_BUTTON_LEFT)) {
		S.notebook_page = (S.notebook_page + NB_PAGES - 1) % NB_PAGES;
		audio_ui(UI_PAGE);
	}
	if (in_take(WPAD_BUTTON_RIGHT)) {
		S.notebook_page = (S.notebook_page + 1) % NB_PAGES;
		audio_ui(UI_PAGE);
	}
	if (S.notebook_page == NB_NOTES) {
		if (in_take(WPAD_BUTTON_UP)) {
			S.notebook_sel = (S.notebook_sel + NOTE_COUNT - 1) % NOTE_COUNT;
			audio_ui(UI_TICK);
		}
		if (in_take(WPAD_BUTTON_DOWN)) {
			S.notebook_sel = (S.notebook_sel + 1) % NOTE_COUNT;
			audio_ui(UI_TICK);
		}
		if (in_take(WPAD_BUTTON_A) && S.found[S.notebook_sel]) {
			P.mode = MODE_READ;
			P.reading = S.notebook_sel;
			audio_ui(UI_PAGE);
		}
	}
	if (S.notebook_page == NB_SOUNDS) {
		if (in_take(WPAD_BUTTON_UP)) {
			S.sound_sel = (S.sound_sel + SND_COUNT - 1) % SND_COUNT;
			audio_ui(UI_TICK);
		}
		if (in_take(WPAD_BUTTON_DOWN)) {
			S.sound_sel = (S.sound_sel + 1) % SND_COUNT;
			audio_ui(UI_TICK);
		}
	}
	(void)dt;
}

/* ------------------------------------------------------------------------ */

/* A pencilled box, ticked when done. */
static void check_box(float x, float y, bool done, float a)
{
	GXColor pen = gx_col(rgb_from(UI_INK), 0.75f * a), red = gx_col(rgb_make(0.55f, 0.18f, 0.14f), 0.9f * a);
	mat_vcolor();
	vdesc_2d(false);
	const float s = 15.0f;
	GX_Begin(GX_LINESTRIP, VF_2D, 5);
	GX_Position2f32(x, y); GX_Color4u8(pen.r, pen.g, pen.b, pen.a);
	GX_Position2f32(x + s, y + 0.6f); GX_Color4u8(pen.r, pen.g, pen.b, pen.a);
	GX_Position2f32(x + s - 0.4f, y + s); GX_Color4u8(pen.r, pen.g, pen.b, pen.a);
	GX_Position2f32(x + 0.3f, y + s - 0.5f); GX_Color4u8(pen.r, pen.g, pen.b, pen.a);
	GX_Position2f32(x, y); GX_Color4u8(pen.r, pen.g, pen.b, pen.a);
	GX_End();
	if (!done) return;
	GX_Begin(GX_LINESTRIP, VF_2D, 3);
	GX_Position2f32(x + 2.0f, y + 7.0f); GX_Color4u8(red.r, red.g, red.b, red.a);
	GX_Position2f32(x + 6.5f, y + 13.0f); GX_Color4u8(red.r, red.g, red.b, red.a);
	GX_Position2f32(x + s + 3.0f, y - 4.0f); GX_Color4u8(red.r, red.g, red.b, red.a);
	GX_End();
}

/* A duty line: a box and up to three wrapped lines of text. Returns the next y. */
static float duty(float x, float y, float w, const char *text, bool done, float a)
{
	GXColor ink = gx_col(rgb_from(UI_INK), (done ? 0.5f : 0.9f) * a);
	int st[3], ln[3];
	int n = text_wrap(FONT_L, text, w - 40.0f, st, ln, 3);
	float lh = text_line_height(FONT_L) * 0.9f;
	gfx_blend_alpha();
	check_box(x, y - 15.0f, done, a);
	text_begin();
	for (int i = 0; i < n; i++) text_draw(FONT_L, x + 32.0f, y + i * lh, text + st[i], ln[i], ink);
	return y + n * lh + 18.0f;
}

/* Basin's forecast, as Theo wrote it down at the morning call. */
static const char *FORECAST[] = {
	[WX_CLEAR] = "Forecast: clear, light winds.",
	[WX_WARM] = "Forecast: clear and warm.",
	[WX_HIGH_CLOUD] = "Forecast: high cloud, breezy by afternoon.",
	[WX_BREEZY] = "Forecast: breezy, westerly.",
	[WX_STORM] = "Forecast: dry thunderstorms from 14:00.",
	[WX_SMOKE] = "Forecast: smoke from Harrow, light winds.",
	[WX_HAZE] = "Forecast: haze from fires to the south.",
	[WX_COLD] = "Forecast: clear and cold.",
	[WX_FROST] = "Forecast: frost early, then clear.",
	[WX_RAIN] = "Forecast: clouding over, rain by noon.",
};

static void today_page(float x, float y, float w, float h, float a)
{
	GXColor ink = gx_col(rgb_from(UI_INK), 0.9f * a), faint = gx_col(rgb_from(UI_INK), 0.5f * a);
	const day_def *di = day_info(S.day);
	char head[96];
	snprintf(head, sizeof(head), "%s. %s", di->title, di->date);
	text_begin();
	text_draw(FONT_L, x + 30.0f, y + 40.0f, head, -1, ink);
	text_draw(FONT_S, x + 30.0f, y + 66.0f, FORECAST[di->weather], -1, faint);
	float ly = y + 128.0f;
	if (S.day < NUM_DAYS || !story_flag(FLAG_SEASON_OVER)) {
		ly = duty(x + 30.0f, ly, w - 60.0f, "Morning readings at the instrument shelter, before noon.", S.obs_am, a);
		ly = duty(x + 30.0f, ly, w - 60.0f, "Evening readings, after five. Call them in.", S.obs_pm, a);
	}
	const char *errand = story_errand();
	if (errand) ly = duty(x + 30.0f, ly, w - 60.0f, errand, story_errand_done(), a);
	else {
		text_begin();
		text_draw(FONT_S, x + 62.0f, ly, "Nothing else from Basin yet.", -1, faint);
	}
	(void)h;
}

static void map_page(float x, float y, float w, float h, float a)
{
	GXColor ink = gx_col(rgb_from(UI_INK), 0.9f * a);
	if (!S.found[NOTE_MAP]) {
		const char *msg = "No map yet. Augie never threw anything away " "\xE2\x80\x94" " it'll be in the cab somewhere.";
		int st[4], ln[4];
		int n = text_wrap(FONT_L, msg, w * 0.7f, st, ln, 4);
		text_begin();
		for (int i = 0; i < n; i++) {
			float lw = text_width(FONT_L, msg + st[i], ln[i]);
			text_draw(FONT_L, x + (w - lw) * 0.5f, y + h * 0.45f + i * text_line_height(FONT_L) * 0.85f, msg + st[i], ln[i], ink);
		}
		return;
	}
	/* The map itself, a little inset like a page glued in. */
	float s = fminf(w, h) - 8.0f, mx = x + (w - s) * 0.5f, my = y + (h - s) * 0.5f;
	gfx_blend_alpha();
	mat_vcolor_tex(&T_MAP, false, false);
	vdesc_2d(true);
	u8 al = (u8)(255 * a);
	GX_Begin(GX_QUADS, VF_2D, 4);
	GX_Position2f32(mx, my); GX_Color4u8(255, 255, 255, al); GX_TexCoord2f32(0, 0);
	GX_Position2f32(mx + s, my); GX_Color4u8(255, 255, 255, al); GX_TexCoord2f32(1, 0);
	GX_Position2f32(mx + s, my + s); GX_Color4u8(255, 255, 255, al); GX_TexCoord2f32(1, 1);
	GX_Position2f32(mx, my + s); GX_Color4u8(255, 255, 255, al); GX_TexCoord2f32(0, 1);
	GX_End();
	/* "You are here": a pencil ring and a tick for the way you face. */
	float px = mx + P.pos.x / WORLD_SIZE * s, py = my + P.pos.z / WORLD_SIZE * s;
	mat_vcolor();
	vdesc_2d(false);
	GXColor pen = gx_col(rgb_make(0.55f, 0.18f, 0.14f), 0.9f * a);
	GX_Begin(GX_LINESTRIP, VF_2D, 17);
	for (int i = 0; i <= 16; i++) {
		float t = LL_TAU * i / 16.0f;
		GX_Position2f32(px + cosf(t) * 6.0f, py + sinf(t) * 6.0f);
		GX_Color4u8(pen.r, pen.g, pen.b, pen.a);
	}
	GX_End();
	GX_Begin(GX_LINES, VF_2D, 2);
	GX_Position2f32(px, py);
	GX_Color4u8(pen.r, pen.g, pen.b, pen.a);
	GX_Position2f32(px + sinf(P.yaw) * 14.0f, py - cosf(P.yaw) * 14.0f);
	GX_Color4u8(pen.r, pen.g, pen.b, pen.a);
	GX_End();
}

static void notes_page(float x, float y, float w, float h, float a)
{
	GXColor ink = gx_col(rgb_from(UI_INK), 0.9f * a), faint = gx_col(rgb_from(UI_INK), 0.35f * a);
	float lh = (h - 20.0f) / NOTE_COUNT;
	mat_vcolor();
	gfx_blend_alpha();
	ui_rect(x + 10, y + 8 + S.notebook_sel * lh, w - 20, lh - 2, gx_col(rgb_make(0.85f, 0.80f, 0.66f), 0.7f * a));
	text_begin();
	for (int i = 0; i < NOTE_COUNT; i++) {
		char line[64];
		float ly = y + 8 + i * lh + lh * 0.72f;
		if (S.found[i]) {
			snprintf(line, sizeof(line), "%d.  %s", i + 1, note_title(i));
			text_draw(FONT_S, x + 24, ly, line, -1, ink);
		} else {
			snprintf(line, sizeof(line), "%d.  . . .", i + 1);
			text_draw(FONT_S, x + 24, ly, line, -1, faint);
		}
	}
}

static void radio_page(float x, float y, float w, float h, float a)
{
	const radio_log_entry *log;
	int n = radio_log(&log);
	GXColor ink = gx_col(rgb_from(UI_INK), 0.9f * a);
	GXColor dot = gx_col(rgb_make(0.62f, 0.36f, 0.16f), a), theo = gx_col(rgb_make(0.25f, 0.38f, 0.5f), a);
	text_begin();
	if (!n) {
		const char *msg = "Nothing on the radio yet.";
		text_draw(FONT_L, x + (w - text_width(FONT_L, msg, -1)) * 0.5f, y + h * 0.5f, msg, -1, ink);
		return;
	}
	/* Newest at the bottom; walk back up until the page is full. */
	float lh = text_line_height(FONT_S) * 0.92f, by = y + h - 14.0f;
	for (int i = n - 1; i >= 0 && by > y + 20.0f; i--) {
		int st[4], ln[4];
		const char *name = log[i].who == SPEAKER_DOT ? "Dot" : (log[i].who == SPEAKER_CREW ? "Crew" : "Theo");
		float nw = 60.0f;
		int lines = text_wrap(FONT_S, log[i].text, w - nw - 40.0f, st, ln, 4);
		float top = by - lines * lh;
		if (top < y + 10.0f) break;
		text_draw(FONT_S, x + 16, top + lh * 0.8f, name, -1, log[i].who == SPEAKER_THEO ? theo : dot);
		for (int k = 0; k < lines; k++)
			text_draw(FONT_S, x + 16 + nw, top + lh * (0.8f + k), log[i].text + st[k], ln[k], ink);
		by = top - 6.0f;
	}
}

/* The sound log: twelve lines, and what Theo wrote about the one picked out. */
static void sounds_page(float x, float y, float w, float h, float a)
{
	GXColor ink = gx_col(rgb_from(UI_INK), 0.9f * a), faint = gx_col(rgb_from(UI_INK), 0.35f * a);
	float list_h = h - 92.0f, lh = list_h / SND_COUNT;
	mat_vcolor();
	gfx_blend_alpha();
	ui_rect(x + 10, y + 6 + S.sound_sel * lh, w - 20, lh - 2, gx_col(rgb_make(0.85f, 0.80f, 0.66f), 0.7f * a));
	text_begin();
	for (int i = 0; i < SND_COUNT; i++) {
		char line[80];
		float ly = y + 6 + i * lh + lh * 0.74f;
		if (S.sound_day[i]) {
			snprintf(line, sizeof(line), "S%02d  %s", i + 1, sound_name(i));
			text_draw(FONT_S, x + 24, ly, line, -1, ink);
			snprintf(line, sizeof(line), "Day %d", S.sound_day[i]);
			text_draw(FONT_S, x + w - 30 - text_width(FONT_S, line, -1), ly, line, -1, faint);
		} else {
			snprintf(line, sizeof(line), "S%02d  . . .", i + 1);
			text_draw(FONT_S, x + 24, ly, line, -1, faint);
		}
	}
	/* The picked-out entry, in full, under a pencil rule. */
	mat_vcolor();
	ui_rect(x + 24, y + list_h + 12, w - 48, 1.5f, faint);
	const char *note = S.sound_day[S.sound_sel] ? sound_note(S.sound_sel)
	                   : (sounds_logged() ? "Not yet. Hold C and keep still." : "Sounds worth keeping. Hold C to listen.");
	int st[3], ln[3];
	int n = text_wrap(FONT_S, note, w - 60.0f, st, ln, 3);
	text_begin();
	for (int i = 0; i < n; i++)
		text_draw(FONT_S, x + 30, y + list_h + 40 + i * text_line_height(FONT_S) * 0.9f, note + st[i], ln[i],
		          S.sound_day[S.sound_sel] ? ink : faint);
}

void notebook_draw(void)
{
	float a = P.notebook_fade;
	if (a < 0.01f) return;
	float w = 600.0f, h = 396.0f;
	float x = (G.ui_w - w) * 0.5f, y = (G.ui_h - h) * 0.5f + 22.0f + (1.0f - a) * 14.0f;
	mat_vcolor();
	gfx_blend_alpha();
	ui_rect(0, 0, G.ui_w, G.ui_h, (GXColor){ 10, 10, 14, (u8)(110 * a) });
	ui_rect(x + 6, y + 7, w, h, (GXColor){ 0, 0, 0, (u8)(70 * a) });
	rgbf pc = rgb_from(UI_PAPER);
	ui_rect_grad(x, y, w, h, gx_col(pc, a), gx_col(rgb_scale(pc, 0.93f), a));
	/* Tabs. */
	float tx = x + 24.0f;
	for (int i = 0; i < NB_PAGES; i++) {
		float tw = text_width(FONT_L, TABS[i], -1);
		bool on = i == S.notebook_page;
		mat_vcolor();
		ui_rect(tx - 10, y - 40, tw + 20, 42, gx_col(rgb_scale(pc, on ? 1.0f : 0.84f), a));
		text_begin();
		text_draw(FONT_L, tx, y - 9, TABS[i], -1, gx_col(rgb_from(UI_INK), (on ? 0.95f : 0.5f) * a));
		tx += tw + 28.0f;
	}
	/* The day, top right. */
	char day[48];
	snprintf(day, sizeof(day), "Day %d, %s", S.day, day_info(S.day)->date);
	text_begin();
	text_draw(FONT_S, x + w - 24 - text_width(FONT_S, day, -1), y - 8, day, -1, gx_col(rgb_from(UI_INK), 0.6f * a));

	float px = x + 14, py = y + 12, pw = w - 28, ph = h - 24;
	switch (S.notebook_page) {
	case NB_TODAY: today_page(px, py, pw, ph, a); break;
	case NB_MAP: map_page(px, py, pw, ph, a); break;
	case NB_NOTES: notes_page(px, py, pw, ph, a); break;
	case NB_RADIO: radio_page(px, py, pw, ph, a); break;
	default: sounds_page(px, py, pw, ph, a); break;
	}
}
