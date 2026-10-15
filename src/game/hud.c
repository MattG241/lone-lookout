/*
 * hud.c -- the minimal UI: a soft reticle with a one-word prompt, the note
 * card, the seated hint, the day title card and a developer overlay.
 * Nothing is on screen unless it is needed.
 */
#include <math.h>
#include <stdio.h>
#include <wiiuse/wpad.h>
#include "game/hud.h"
#include "game/game.h"
#include "game/radio.h"
#include "input/input.h"
#include "gfx/gfx.h"
#include "gfx/text.h"
#include "palette_data.h"

static void ring(float cx, float cy, float r, float thick, GXColor c)
{
	const int seg = 20;
	mat_vcolor();
	vdesc_2d(false);
	GX_Begin(GX_TRIANGLESTRIP, VF_2D, (seg + 1) * 2);
	for (int i = 0; i <= seg; i++) {
		float a = LL_TAU * i / seg;
		float ca = cosf(a), sa = sinf(a);
		GX_Position2f32(cx + ca * (r + thick), cy + sa * (r + thick));
		GX_Color4u8(c.r, c.g, c.b, c.a);
		GX_Position2f32(cx + ca * r, cy + sa * r);
		GX_Color4u8(c.r, c.g, c.b, c.a);
	}
	GX_End();
}

static void dot(float cx, float cy, float r, GXColor c)
{
	const int seg = 12;
	mat_vcolor();
	vdesc_2d(false);
	GX_Begin(GX_TRIANGLEFAN, VF_2D, seg + 2);
	GX_Position2f32(cx, cy);
	GX_Color4u8(c.r, c.g, c.b, c.a);
	for (int i = 0; i <= seg; i++) {
		float a = LL_TAU * i / seg;
		GX_Position2f32(cx + cosf(a) * r, cy + sinf(a) * r);
		GX_Color4u8(c.r, c.g, c.b, c.a / 3);
	}
	GX_End();
}

static void draw_reticle(void)
{
	static float s_hover_a;
	int n;
	const interactable_t *ia = world_interactables(&n);
	bool hot = P.hover >= 0;
	s_hover_a += ((hot ? 1.0f : 0.0f) - s_hover_a) * 0.25f;
	if (radio_wants_pointer() || P.mode == MODE_READ) {
		/* Choosing a card: the reticle becomes a plain pointer dot. */
		if (IN.ir_valid) {
			gfx_blend_alpha();
			dot((IN.ir_x * 0.5f + 0.5f) * G.ui_w, (IN.ir_y * 0.5f + 0.5f) * G.ui_h, 4.0f,
			    (GXColor){ 245, 238, 222, 220 });
		}
		return;
	}
	float x = (P.reticle_x * 0.5f + 0.5f) * G.ui_w;
	float y = (P.reticle_y * 0.5f + 0.5f) * G.ui_h;
	gfx_blend_alpha();
	GXColor paper = gx_col(rgb_from(UI_SUBTITLE), 1.0f);
	paper.a = (u8)(110 + 100 * s_hover_a);
	dot(x, y, 2.6f + s_hover_a, paper);
	if (s_hover_a > 0.02f) {
		GXColor rc = paper;
		rc.a = (u8)(180 * s_hover_a);
		ring(x, y, 7.0f + 2.0f * s_hover_a, 1.4f, rc);
		if (hot) {
			text_begin();
			GXColor tc = paper;
			tc.a = (u8)(235 * s_hover_a);
			text_draw_soft(FONT_S, x + 16.0f, y + 6.0f, ia[P.hover].verb, -1, tc, s_hover_a);
		}
	}
}

static void draw_note(void)
{
	if (P.read_fade < 0.01f || P.reading < 0) return;
	float a = P.read_fade;
	const char *s = note_text(P.reading);
	float w = 470.0f;
	float lh = text_line_height(FONT_L) * 0.84f;
	/* Count wrapped lines first so the card fits its text. */
	int total = 0;
	{
		const char *q = s;
		while (*q) {
			const char *nl = q;
			while (*nl && *nl != '\n') nl++;
			char buf[256];
			int plen = (int)(nl - q) < 255 ? (int)(nl - q) : 255;
			for (int i = 0; i < plen; i++) buf[i] = q[i];
			buf[plen] = 0;
			int st[12], ln[12];
			total += text_wrap(FONT_L, buf, w - 56.0f, st, ln, 12);
			q = *nl ? nl + 1 : nl;
		}
	}
	float h = total * lh + 64.0f;
	float x = (G.ui_w - w) * 0.5f, y = (G.ui_h - h) * 0.5f + (1.0f - a) * 12.0f;
	mat_vcolor();
	gfx_blend_alpha();
	ui_rect(0, 0, G.ui_w, G.ui_h, (GXColor){ 10, 10, 14, (u8)(90 * a) });
	ui_rect(x + 5, y + 6, w, h, (GXColor){ 0, 0, 0, (u8)(70 * a) });
	rgbf pc = rgb_from(UI_PAPER);
	ui_rect_grad(x, y, w, h, gx_col(pc, a), gx_col(rgb_scale(pc, 0.94f), a));
	/* A strip of tape at the top. */
	ui_rect(x + w * 0.5f - 34, y - 9, 68, 20, gx_col(rgb_make(0.86f, 0.83f, 0.72f), 0.85f * a));

	text_begin();
	GXColor ink = gx_col(rgb_from(UI_INK), 0.92f * a);
	int starts[10], lens[10];
	int lines = 0;
	/* Respect explicit line breaks, wrapping each paragraph. */
	const char *para = s;
	float ly = y + 30.0f + text_ascent(FONT_L);
	while (*para && lines < 10) {
		const char *nl = para;
		while (*nl && *nl != '\n') nl++;
		char buf[256];
		int plen = (int)(nl - para);
		if (plen > 255) plen = 255;
		for (int i = 0; i < plen; i++) buf[i] = para[i];
		buf[plen] = 0;
		int n = text_wrap(FONT_L, buf, w - 56.0f, starts, lens, 10 - lines);
		for (int i = 0; i < n; i++, lines++) {
			text_draw(FONT_L, x + 28.0f, ly, buf + starts[i], lens[i], ink);
			ly += lh;
		}
		para = *nl ? nl + 1 : nl;
	}
}

static void draw_seated_hint(void)
{
	static float s_seated_t;
	if (P.mode != MODE_SIT) {
		s_seated_t = 0.0f;
		return;
	}
	s_seated_t += 1.0f / 60.0f;
	float a = saturatef(s_seated_t - 0.8f) * (1.0f - saturatef((s_seated_t - 7.0f) * 0.5f));
	a = fmaxf(a, P.timelapse * 0.8f);
	if (a < 0.01f) return;
	text_begin();
	GXColor c = gx_col(rgb_from(UI_SUBTITLE), a);
	if (P.timelapse > 0.05f) {
		char clock[16];
		int hh = (int)DAY.hour, mm = (int)((DAY.hour - hh) * 60.0f);
		snprintf(clock, sizeof(clock), "%02d:%02d", hh, mm);
		const char *msg = "time passing";
		float w = text_width(FONT_S, msg, -1);
		text_draw_soft(FONT_S, (G.ui_w - w) * 0.5f, 52.0f, msg, -1, c, a);
		float cw = text_width(FONT_L, clock, -1);
		text_draw_soft(FONT_L, (G.ui_w - cw) * 0.5f, 92.0f, clock, -1, c, a);
	} else {
		const char *msg = "Hold Z to let time pass  -  A to stand";
		float w = text_width(FONT_S, msg, -1);
		text_draw_soft(FONT_S, (G.ui_w - w) * 0.5f, 452.0f, msg, -1, c, a);
	}
}

static void draw_day_card(void)
{
	float t = P.day_card;
	float a = saturatef((t - 0.6f) / 1.2f) * (1.0f - saturatef((t - 5.5f) / 1.5f));
	if (a < 0.01f) return;
	text_begin();
	GXColor c = gx_col(rgb_from(UI_SUBTITLE), a);
	const char *title = "Day 1";
	const char *sub = "21 August";
	float w1 = text_width(FONT_L, title, -1), w2 = text_width(FONT_S, sub, -1);
	text_draw_soft(FONT_L, (G.ui_w - w1) * 0.5f, 120.0f, title, -1, c, a);
	text_draw_soft(FONT_S, (G.ui_w - w2) * 0.5f, 150.0f, sub, -1, c, a * 0.9f);
}

static void draw_dev_overlay(void)
{
#if defined(LL_SHOT_TOUR)
	return;
#elif !defined(LL_INPUT_SCRIPT)
	if (!(in_held(WPAD_BUTTON_1) && in_held(WPAD_BUTTON_2))) return;
#endif
	char line[160];
	text_begin();
	GXColor c = { 255, 250, 235, 230 };
	snprintf(line, sizeof(line), "%.1f ms  %s fps  %02d:%02d  pos %.1f %.1f %.1f  yaw %.0f pitch %.0f roll %.1f",
	         G.work_ms, G.vsyncs == 1 ? "60" : "30", (int)DAY.hour, (int)((DAY.hour - (int)DAY.hour) * 60),
	         P.pos.x, P.pos.y, P.pos.z, RAD2DEG(P.yaw), RAD2DEG(P.pitch), RAD2DEG(P.roll));
	text_draw_soft(FONT_S, 20, 30, line, -1, c, 1.0f);
	snprintf(line, sizeof(line), "mode %d  hover %d  radio %s  surface %d",
	         (int)P.mode, P.hover, radio_busy() ? (radio_in_call() ? "call" : "ring") : "idle", (int)P.surface);
	text_draw_soft(FONT_S, 20, 54, line, -1, c, 1.0f);
}

void hud_draw(void)
{
	gfx_ortho();
	gfx_depth(false, false);
	draw_day_card();
	draw_reticle();
	draw_seated_hint();
	radio_draw();
	draw_note();
	draw_dev_overlay();
	gfx_blend_none();
}
