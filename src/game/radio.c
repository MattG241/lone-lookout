/*
 * radio.c -- the radio as the player meets it: the call chirp, raising the
 * handset (B), subtitles, choice cards, and the log. The script itself runs
 * in dialogue.c; this module only times lines, takes input and draws.
 * Choices never time out; "[say nothing]" is always offered explicitly
 * (DESIGN.md section 4).
 */
#include <string.h>
#include <wiiuse/wpad.h>
#include "game/radio.h"
#include "game/game.h"
#include "game/story.h"
#include "audio/audio.h"
#include "audio/vo.h"
#include "game/playtest.h"
#include "input/input.h"
#include "gfx/gfx.h"
#include "gfx/text.h"
#include "palette_data.h"

#define RING_GIVE_UP 45.0f
#define MAX_OPT 4

static struct {
	dlg_state last;
	float t;                  /* time in the current state or line */
	float line_len;
	float ring_t, next_chirp;
	int hovered;
	float sub_alpha;
	char sub_text[400];
	int sub_who;
	float hold_hiss;          /* Day 6: the carrier stays open after Dot falls asleep */
	bool vo_wait;             /* waiting on a recorded line from the SD card */
} R;

static radio_log_entry s_log[RADIO_LOG_MAX];
static char s_log_text[RADIO_LOG_MAX][200];
static int s_log_n;

static void log_line(int who, const char *text)
{
	if (s_log_n == RADIO_LOG_MAX) {
		memmove(s_log, s_log + 1, sizeof(s_log[0]) * (RADIO_LOG_MAX - 1));
		memmove(s_log_text, s_log_text + 1, sizeof(s_log_text[0]) * (RADIO_LOG_MAX - 1));
		s_log_n--;
	}
	strncpy(s_log_text[s_log_n], text, sizeof(s_log_text[0]) - 1);
	s_log_text[s_log_n][sizeof(s_log_text[0]) - 1] = 0;
	s_log[s_log_n] = (radio_log_entry){ S.day, (speaker_t)who, s_log_text[s_log_n] };
	s_log_n++;
}

void radio_log_save(sw_t *w)
{
	sw_u8(w, (u8)s_log_n);
	for (int i = 0; i < s_log_n; i++) {
		sw_u8(w, (u8)s_log[i].day);
		sw_u8(w, (u8)s_log[i].who);
		sw_str(w, s_log_text[i]);
	}
}

void radio_log_load(sr_t *r)
{
	int n = sr_u8(r);
	s_log_n = 0;
	for (int i = 0; i < n && !r->bad; i++) {
		int day = sr_u8(r), who = sr_u8(r);
		char text[200];
		sr_str(r, text, sizeof(text));
		if (who > SPEAKER_CREW) continue;
		log_line(who, text);
		s_log[s_log_n - 1].day = day;
	}
}

int radio_log(const radio_log_entry **out)
{
	/* Re-point the texts: the entries move when the log scrolls. */
	for (int i = 0; i < s_log_n; i++) s_log[i].text = s_log_text[i];
	*out = s_log;
	return s_log_n;
}

void radio_init(void)
{
	memset(&R, 0, sizeof(R));
	s_log_n = 0;
	dlg_init();
}

bool radio_busy(void) { return dlg_get_state() != DS_IDLE; }
bool radio_in_call(void) { dlg_state s = dlg_get_state(); return s == DS_LINE || s == DS_CHOICE; }
bool radio_wants_pointer(void) { return dlg_get_state() == DS_CHOICE; }
void radio_hold_hiss(float seconds) { R.hold_hiss = seconds; }

static bool garbled(int speaker) { return dlg_current_garbled() && speaker != SPEAKER_THEO; }

static void murmur(void)
{
	R.line_len = R.t + voice_line_seconds(R.sub_text, (speaker_t)R.sub_who) + 0.8f;
	audio_voice_line_ex(R.sub_text, (speaker_t)R.sub_who, garbled(R.sub_who));
}

/* A line starts: voice it (a recording if the card has one), log it, time it. */
void dlg_hook_line(int speaker, const char *text)
{
	strncpy(R.sub_text, text, sizeof(R.sub_text) - 1);
	R.sub_who = speaker;
	R.t = 0.0f;
	char id[48];
	R.vo_wait = vo_available() && dlg_line_id(id, sizeof(id));
	if (R.vo_wait) {
		vo_request(id);
		R.line_len = 1e9f;
	} else {
		murmur();
	}
	if (!P.input_locked) log_line(speaker, text);
}

/* The recording arrived, or there isn't one (or the card is slow): start speaking. */
static void vo_check(void)
{
	const s16 *pcm;
	int len;
	vo_status st = vo_poll(&pcm, &len);
	if (st == VO_READY) {
		audio_voice_pcm(pcm, len, (speaker_t)R.sub_who, garbled(R.sub_who));
		R.line_len = R.t + len / 22050.0f + 0.6f;
		R.vo_wait = false;
	} else if (st != VO_PENDING || R.t > 0.6f) {
		murmur();
		R.vo_wait = false;
	}
}

void radio_call_basin(void)
{
	dlg_world w = story_world();
	w.can_ring = true;
	if (dlg_get_state() == DS_IDLE && dlg_answer(&w)) {
		audio_ui(UI_KEY_UP);
		audio_radio_hiss(true);
		playtest_event("call_basin", "%s", dlg_call_id(dlg_current_call()));
	}
}

/* Choice card layout on the UI canvas. */
#define CARD_X 48.0f
#define CARD_W 360.0f
#define CARD_H 28.0f
#define CARD_GAP 6.0f
static float card_y(int i, int n) { return 392.0f - (n - i) * (CARD_H + CARD_GAP); }

void radio_update(float dt)
{
	dlg_world w = story_world();
	dlg_tick(&w, dt);
	dlg_state st = dlg_get_state();
	if (st != R.last) {
		if (st == DS_RINGING) {
			R.ring_t = 0.0f;
			R.next_chirp = 0.0f;
			playtest_event("ring", "%s", dlg_call_id(dlg_current_call()));
		}
		if (st == DS_CHOICE) R.hovered = 0;
		if (st == DS_CLOSING) {
			audio_ui(UI_SQUELCH);
			if (R.hold_hiss <= 0.0f) audio_radio_hiss(false);
		}
		if (st == DS_CHOICE || st == DS_CLOSING || st == DS_IDLE) R.t = 0.0f;
		R.last = st;
	}
	R.t += dt;
	bool showing = st == DS_LINE || st == DS_CHOICE;
	R.sub_alpha = approachf(R.sub_alpha, showing ? 1.0f : 0.0f, showing ? 9.0f : 4.0f, dt);
	if (R.hold_hiss > 0.0f && st == DS_IDLE) {
		R.hold_hiss -= dt;
		if (R.hold_hiss <= 0.0f || (!P.input_locked && in_down(WPAD_BUTTON_B))) {
			R.hold_hiss = 0.0f;
			audio_radio_hiss(false);
			audio_ui(UI_SQUELCH);
		}
	}

	switch (st) {
	case DS_IDLE:
		/* B with nothing ringing: call Basin yourself. */
		if (!P.input_locked && in_down(WPAD_BUTTON_B) && game_radio_allowed() && R.hold_hiss <= 0.0f)
			radio_call_basin();
		break;
	case DS_RINGING:
		R.ring_t += dt;
		if (R.ring_t >= R.next_chirp) {
			audio_ui(UI_CALL_CHIRP);
			R.next_chirp += 3.5f;
		}
		if (!P.input_locked && in_down(WPAD_BUTTON_B) && P.mode != MODE_SLEEP) {
			playtest_event("answer", "%s\t%.0f", dlg_call_id(dlg_current_call()), R.ring_t);
			audio_ui(UI_KEY_UP);
			audio_radio_hiss(true);
			dlg_answer(&w);
		} else if (R.ring_t > RING_GIVE_UP) {
			/* Unanswered: Dot will try again later. Never a penalty. */
			playtest_event("missed", "%s", dlg_call_id(dlg_current_call()));
			dlg_unanswered(&w);
		}
		break;
	case DS_LINE:
		if (R.vo_wait) vo_check();
		if ((!OPT.manual_dialogue && R.t > R.line_len) ||
		    (!P.input_locked && R.t > 0.35f && in_take(WPAD_BUTTON_A))) {
			if (R.t <= R.line_len) audio_voice_stop();
			dlg_next();
		}
		break;
	case DS_CHOICE: {
		const char *opts[MAX_OPT];
		int n = dlg_options(opts, MAX_OPT);
		if (P.input_locked) break;
		if (in_down(WPAD_BUTTON_UP)) { R.hovered = (R.hovered + n - 1) % n; audio_ui(UI_TICK); }
		if (in_down(WPAD_BUTTON_DOWN)) { R.hovered = (R.hovered + 1) % n; audio_ui(UI_TICK); }
		if (IN.ir_valid) {
			float px = (IN.ir_x * 0.5f + 0.5f) * G.ui_w, py = (IN.ir_y * 0.5f + 0.5f) * G.ui_h;
			for (int i = 0; i < n; i++) {
				float y = card_y(i, n);
				if (px >= CARD_X - 8 && px <= CARD_X + CARD_W && py >= y && py <= y + CARD_H && R.hovered != i) {
					R.hovered = i;
					audio_ui(UI_TICK);
				}
			}
		}
		if (R.t > 0.3f && in_down(WPAD_BUTTON_A)) {
			audio_ui(UI_PAGE);
			int pick = R.hovered < n ? R.hovered : 0;
			playtest_event("choice", "%s\t%d\t%s", dlg_call_id(dlg_current_call()), pick, opts[pick]);
			dlg_choose(pick);
		}
		break;
	}
	case DS_CLOSING:
		if (R.t > 1.2f) dlg_close();
		break;
	}
}

/* ------------------------------------------------------------------------ */

static void draw_radio_glyph(float x, float y, float pulse, GXColor ink, GXColor accent)
{
	mat_vcolor();
	ui_rect(x, y + 4, 10, 15, ink);
	ui_rect(x + 2, y + 6, 6, 4, (GXColor){ 40, 38, 40, ink.a });
	ui_rect(x + 7, y - 4, 2, 8, ink);
	/* Signal arcs, as short pencil ticks that pulse outward. */
	for (int i = 0; i < 2; i++) {
		float a = saturatef(pulse * 2.0f - i * 0.5f) * (1.0f - pulse * 0.6f);
		GXColor c = accent;
		c.a = (u8)(accent.a * a);
		ui_rect(x + 14 + i * 5, y + 4 - i * 2, 2, 9 + i * 4, c);
	}
}

static const char *speaker_name(int who)
{
	return who == SPEAKER_DOT ? "Dot:" : (who == SPEAKER_THEO ? "Theo:" : "Harrow:");
}

void radio_draw(void)
{
	text_begin();
	GXColor paper = gx_col(rgb_from(UI_SUBTITLE), 1.0f);
	GXColor dotc = gx_col(rgb_from(UI_SPEAKER_DOT), 1.0f);
	GXColor theoc = gx_col(rgb_from(UI_SPEAKER_THEO), 1.0f);
	GXColor crewc = gx_col(rgb_make(0.80f, 0.86f, 0.70f), 1.0f);
	dlg_state st = dlg_get_state();

	if (st == DS_RINGING) {
		float pulse = R.ring_t - (int)(R.ring_t / 3.5f) * 3.5f;
		pulse = saturatef(pulse / 1.2f);
		GXColor ink = paper;
		ink.a = 220;
		draw_radio_glyph(48, 420, pulse, ink, dotc);
		if (R.ring_t > 1.0f) {
			text_begin();
			GXColor h = paper;
			h.a = (u8)(200 * saturatef(R.ring_t - 1.0f));
			text_draw_soft(FONT_S, 80, 436, "B  Radio", -1, h, 1.0f);
		}
	}

	/* Subtitle: speaker in their colour, then the line, up to four lines. */
	if (R.sub_alpha > 0.01f && R.sub_text[0]) {
		GXColor sc = R.sub_who == SPEAKER_DOT ? dotc : (R.sub_who == SPEAKER_THEO ? theoc : crewc);
		const char *name = speaker_name(R.sub_who);
		sc.a = (u8)(255 * R.sub_alpha);
		GXColor tc = paper;
		tc.a = (u8)(245 * R.sub_alpha);
		font_id f = OPT.subs_large ? FONT_L : FONT_S;
		float lh = text_line_height(f);
		float x = 48.0f + 26.0f, y = 430.0f;
		float nx = x + text_width(f, name, -1) + 8.0f;
		int starts[4], lens[4];
		int n = text_wrap(f, R.sub_text, G.ui_w - nx - 60.0f, starts, lens, 4);
		float ly = y - (n - 1) * lh;
		if (OPT.subs_backing) {
			mat_vcolor();
			gfx_blend_alpha();
			ui_rect(x - 40.0f, ly - lh * 0.95f, G.ui_w - x - 20.0f, n * lh + lh * 0.5f,
			        (GXColor){ 10, 10, 14, (u8)(150 * R.sub_alpha) });
		}
		draw_radio_glyph(x - 28, ly - 15, 0.0f, tc, sc);
		text_begin();
		text_draw_soft(f, x, ly, name, -1, sc, R.sub_alpha);
		for (int i = 0; i < n; i++)
			text_draw_soft(f, nx, ly + i * lh, R.sub_text + starts[i], lens[i], tc, R.sub_alpha);
		if (OPT.manual_dialogue && st == DS_LINE && R.t > R.line_len) {
			text_draw_soft(FONT_S, G.ui_w - 155.0f, 462.0f, "A  Continue", -1, paper, R.sub_alpha);
		}
	}

	/* Choice cards: little paper slips with pencil text. */
	if (st == DS_CHOICE) {
		const char *opts[MAX_OPT];
		int n = dlg_options(opts, MAX_OPT);
		float a = saturatef(R.t * 4.0f);
		for (int i = 0; i < n; i++) {
			bool hot = i == R.hovered;
			float y = card_y(i, n) - (hot ? 2.0f : 0.0f);
			float x = CARD_X + (hot ? 6.0f : 0.0f);
			float w = fmaxf(CARD_W, text_width(FONT_S, opts[i], -1) + 30.0f);
			mat_vcolor();
			gfx_blend_alpha();
			rgbf pc = rgb_from(UI_PAPER);
			ui_rect(x + 3, y + 3, w, CARD_H, (GXColor){ 0, 0, 0, (u8)(60 * a) });
			ui_rect_grad(x, y, w, CARD_H, gx_col(pc, (hot ? 0.97f : 0.82f) * a),
			             gx_col(rgb_scale(pc, 0.93f), (hot ? 0.97f : 0.82f) * a));
			if (hot) ui_rect(x, y, 3, CARD_H, gx_col(rgb_from(UI_SPEAKER_THEO), a));
			text_begin();
			GXColor ink = gx_col(rgb_from(UI_INK), (hot ? 1.0f : 0.8f) * a);
			text_draw(FONT_S, x + 12, y + 20, opts[i], -1, ink);
		}
	}
	gfx_blend_none();
}

/* ------------------------------------------------------------------------ */
/* Screenshot tour helpers                                                   */
/* ------------------------------------------------------------------------ */

void radio_debug_call(int call, int lines)
{
	dlg_force_call(call);
	for (int i = 0; i < lines && dlg_get_state() == DS_LINE; i++) dlg_next();
	R.last = dlg_get_state();
	R.t = 1.0f;
	R.hovered = 0;
	R.sub_alpha = 1.0f;
}
