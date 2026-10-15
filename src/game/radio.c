/*
 * radio.c -- radio v0: a tiny node graph per call, played as murmur voices
 * with hand-lettered subtitles. Choices never time out; "[say nothing]" is
 * always offered explicitly (DESIGN.md section 4).
 */
#include <string.h>
#include <wiiuse/wpad.h>
#include "game/radio.h"
#include "game/game.h"
#include "audio/audio.h"
#include "input/input.h"
#include "gfx/gfx.h"
#include "gfx/text.h"
#include "palette_data.h"

typedef struct {
	speaker_t who;
	const char *text;
} line_t;

typedef struct {
	const char *label;        /* what the card says */
	int next;                 /* node to continue with */
} choice_t;

#define MAX_LINES 4
#define MAX_CHOICES 3

typedef struct {
	line_t lines[MAX_LINES];
	int nlines;
	choice_t choices[MAX_CHOICES];
	int nchoices;
	int next;                 /* when there are no choices; -1 ends the call */
} node_t;

/* ---- Day 1: radio check (D1_ARRIVE in the design outline) ---------------- */
static const node_t D1_NODES[] = {
	/* 0 */ { { { SPEAKER_DOT, "Kestrel, Basin. Radio check, over." } }, 1,
	          { { "\"Basin, Kestrel. Loud and clear.\"", 1 }, { "\"Hello? Is this... hello?\"", 2 },
	            { "[say nothing]", 3 } }, 3, -1 },
	/* 1 */ { { { SPEAKER_THEO, "Basin, Kestrel. Loud and clear." },
	            { SPEAKER_DOT, "Well. Look at you. Augie took three days to say it right." } }, 2,
	          { { 0 } }, 0, 5 },
	/* 2 */ { { { SPEAKER_THEO, "Hello? Is this... hello?" },
	            { SPEAKER_DOT, "It's a radio, Kestrel, not a seance." },
	            { SPEAKER_DOT, "Push, talk, let go, listen. Most people get the last part wrong." } }, 3,
	          { { 0 } }, 0, 5 },
	/* 3 */ { { { SPEAKER_DOT, "...Kestrel? You push the button, you talk, you let go. Try again, over." } }, 1,
	          { { "\"Basin, Kestrel. Loud and clear.\"", 1 }, { "\"Hello? Is this... hello?\"", 2 },
	            { "[say nothing]", 4 } }, 3, -1 },
	/* 4 */ { { { SPEAKER_DOT, "Okay. I'll take that as a nod." } }, 1, { { 0 } }, 0, 5 },
	/* 5 */ { { { SPEAKER_DOT, "Welcome to Kestrel. Kettle sticks \xE2\x80\x94 hit it on the left side." },
	            { SPEAKER_DOT, "Call me if you see smoke. Otherwise you're paid to sit. Basin out." } }, 2,
	          { { 0 } }, 0, -1 },
};

/* ---- Player-initiated check-in ----------------------------------------- */
static const node_t CHECKIN_NODES[] = {
	/* 0 */ { { { SPEAKER_THEO, "Basin, Kestrel. Just checking in." },
	            { SPEAKER_DOT, "Copy, Kestrel. All quiet down here. Go look at something pretty." } }, 2,
	          { { 0 } }, 0, -1 },
};

static const struct { const node_t *nodes; int count; } CALLS[CALL_COUNT] = {
	{ D1_NODES, sizeof(D1_NODES) / sizeof(D1_NODES[0]) },
	{ CHECKIN_NODES, 1 },
};

typedef enum { R_IDLE, R_RINGING, R_LINE, R_CHOICE, R_CLOSING } rstate;

static struct {
	rstate state;
	call_id call;
	int node, line;
	float t;                  /* time in current state/line */
	float line_len;
	float ring_t, next_chirp;
	int hovered;
	float sub_alpha;
	const char *sub_text;
	speaker_t sub_who;
	bool d1_done;
} R;

void radio_init(void)
{
	memset(&R, 0, sizeof(R));
	R.hovered = 0;
}

bool radio_busy(void) { return R.state != R_IDLE; }
bool radio_in_call(void) { return R.state == R_LINE || R.state == R_CHOICE; }
bool radio_wants_pointer(void) { return R.state == R_CHOICE; }

void radio_ring(call_id id)
{
	if (R.state != R_IDLE) return;
	R.state = R_RINGING;
	R.call = id;
	R.ring_t = 0.0f;
	R.next_chirp = 0.0f;
}

static const node_t *cur_node(void) { return &CALLS[R.call].nodes[R.node]; }

static void start_line(void)
{
	const line_t *l = &cur_node()->lines[R.line];
	R.state = R_LINE;
	R.t = 0.0f;
	R.line_len = voice_line_seconds(l->text, l->who) + 0.8f;
	R.sub_text = l->text;
	R.sub_who = l->who;
	audio_voice_line(l->text, l->who);
}

static void enter_node(int n)
{
	if (n < 0) {
		R.state = R_CLOSING;
		R.t = 0.0f;
		audio_ui(UI_SQUELCH);
		audio_radio_hiss(false);
		if (R.call == CALL_D1_RADIO_CHECK) R.d1_done = true;
		return;
	}
	R.node = n;
	R.line = 0;
	if (cur_node()->nlines > 0) {
		start_line();
	} else {
		R.state = R_CHOICE;
		R.t = 0.0f;
	}
}

static void finish_line(void)
{
	const node_t *nd = cur_node();
	if (R.line + 1 < nd->nlines) {
		R.line++;
		start_line();
	} else if (nd->nchoices > 0) {
		R.state = R_CHOICE;
		R.t = 0.0f;
		R.hovered = 0;
	} else {
		enter_node(nd->next);
	}
}

/* Choice card layout on the UI canvas. */
#define CARD_X 48.0f
#define CARD_W 330.0f
#define CARD_H 28.0f
#define CARD_GAP 6.0f
static float card_y(int i, int n) { return 392.0f - (n - i) * (CARD_H + CARD_GAP); }

void radio_update(float dt)
{
	R.t += dt;
	bool showing = R.state == R_LINE || R.state == R_CHOICE;
	R.sub_alpha = approachf(R.sub_alpha, showing ? 1.0f : 0.0f, showing ? 9.0f : 4.0f, dt);

	switch (R.state) {
	case R_IDLE:
		/* B with nothing ringing: call Basin yourself. */
		if (!P.input_locked && in_down(WPAD_BUTTON_B) && P.mode != MODE_READ) {
			R.call = CALL_CHECK_IN;
			audio_ui(UI_KEY_UP);
			audio_radio_hiss(true);
			enter_node(0);
		}
		break;
	case R_RINGING:
		R.ring_t += dt;
		if (R.ring_t >= R.next_chirp) {
			audio_ui(UI_CALL_CHIRP);
			R.next_chirp += 3.5f;
		}
		if (!P.input_locked && in_down(WPAD_BUTTON_B)) {
			audio_ui(UI_KEY_UP);
			audio_radio_hiss(true);
			enter_node(0);
		} else if (R.ring_t > 45.0f) {
			/* Unanswered: Dot will try again later. Never a penalty. */
			R.state = R_IDLE;
		}
		break;
	case R_LINE:
		if (R.t > R.line_len || (!P.input_locked && R.t > 0.35f && in_down(WPAD_BUTTON_A))) {
			if (R.t <= R.line_len) audio_voice_stop();
			finish_line();
		}
		break;
	case R_CHOICE: {
		const node_t *nd = cur_node();
		if (P.input_locked) break;
		if (in_down(WPAD_BUTTON_UP)) { R.hovered = (R.hovered + nd->nchoices - 1) % nd->nchoices; audio_ui(UI_TICK); }
		if (in_down(WPAD_BUTTON_DOWN)) { R.hovered = (R.hovered + 1) % nd->nchoices; audio_ui(UI_TICK); }
		if (IN.ir_valid) {
			float px = (IN.ir_x * 0.5f + 0.5f) * G.ui_w, py = (IN.ir_y * 0.5f + 0.5f) * G.ui_h;
			for (int i = 0; i < nd->nchoices; i++) {
				float y = card_y(i, nd->nchoices);
				if (px >= CARD_X - 8 && px <= CARD_X + CARD_W && py >= y && py <= y + CARD_H && R.hovered != i) {
					R.hovered = i;
					audio_ui(UI_TICK);
				}
			}
		}
		if (R.t > 0.3f && in_down(WPAD_BUTTON_A)) {
			audio_ui(UI_PAGE);
			enter_node(nd->choices[R.hovered].next);
		}
		break;
	}
	case R_CLOSING:
		if (R.t > 1.2f) R.state = R_IDLE;
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

void radio_draw(void)
{
	float W = G.ui_w;
	(void)W;
	text_begin();
	GXColor paper = gx_col(rgb_from(UI_SUBTITLE), 1.0f);
	GXColor dotc = gx_col(rgb_from(UI_SPEAKER_DOT), 1.0f);
	GXColor theoc = gx_col(rgb_from(UI_SPEAKER_THEO), 1.0f);

	if (R.state == R_RINGING) {
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

	/* Subtitle: speaker in their colour, then the line, max two lines. */
	if (R.sub_alpha > 0.01f && R.sub_text) {
		GXColor sc = R.sub_who == SPEAKER_DOT ? dotc : theoc;
		const char *name = R.sub_who == SPEAKER_DOT ? "Dot:" : "Theo:";
		sc.a = (u8)(255 * R.sub_alpha);
		GXColor tc = paper;
		tc.a = (u8)(245 * R.sub_alpha);
		float x = 48.0f, y = 430.0f;
		x += 26.0f;
		float nx = x + text_width(FONT_S, name, -1) + 8.0f;
		int starts[3], lens[3];
		int n = text_wrap(FONT_S, R.sub_text, G.ui_w - nx - 60.0f, starts, lens, 3);
		float ly = y - (n - 1) * text_line_height(FONT_S);
		draw_radio_glyph(x - 28, ly - 15, 0.0f, tc, sc);
		text_begin();
		text_draw_soft(FONT_S, x, ly, name, -1, sc, R.sub_alpha);
		for (int i = 0; i < n; i++)
			text_draw_soft(FONT_S, nx, ly + i * text_line_height(FONT_S), R.sub_text + starts[i], lens[i], tc, R.sub_alpha);
	}

	/* Choice cards: little paper slips with pencil text. */
	if (R.state == R_CHOICE) {
		const node_t *nd = cur_node();
		float a = saturatef(R.t * 4.0f);
		for (int i = 0; i < nd->nchoices; i++) {
			bool hot = i == R.hovered;
			float y = card_y(i, nd->nchoices) - (hot ? 2.0f : 0.0f);
			float x = CARD_X + (hot ? 6.0f : 0.0f);
			mat_vcolor();
			gfx_blend_alpha();
			rgbf pc = rgb_from(UI_PAPER);
			ui_rect(x + 3, y + 3, CARD_W, CARD_H, (GXColor){ 0, 0, 0, (u8)(60 * a) });
			ui_rect_grad(x, y, CARD_W, CARD_H, gx_col(pc, (hot ? 0.97f : 0.82f) * a),
			             gx_col(rgb_scale(pc, 0.93f), (hot ? 0.97f : 0.82f) * a));
			if (hot) ui_rect(x, y, 3, CARD_H, gx_col(rgb_from(UI_SPEAKER_THEO), a));
			text_begin();
			GXColor ink = gx_col(rgb_from(UI_INK), (hot ? 1.0f : 0.8f) * a);
			text_draw(FONT_S, x + 12, y + 20, nd->choices[i].label, -1, ink);
		}
	}
	gfx_blend_none();
}

/* ------------------------------------------------------------------------ */

void radio_debug_choices(call_id id, int node, int hovered)
{
	R.call = id;
	R.node = node;
	R.state = R_CHOICE;
	R.t = 1.0f;
	R.hovered = hovered;
	const node_t *nd = cur_node();
	if (nd->nlines > 0) {
		R.sub_text = nd->lines[nd->nlines - 1].text;
		R.sub_who = nd->lines[nd->nlines - 1].who;
		R.sub_alpha = 1.0f;
	}
}

void radio_debug_line(call_id id, int node, int line, float age)
{
	R.call = id;
	R.node = node;
	R.line = line;
	start_line();
	R.t = age;
	R.sub_alpha = 1.0f;
}
