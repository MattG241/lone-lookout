/*
 * hud.c -- the minimal UI: a soft reticle with a one-word prompt, the text
 * card (notes, logbook, supply box), the seated hint, the day card, place
 * names as you arrive, Theo's passing thoughts, the binocular and fire
 * finder views, the notebook, and a developer overlay. Nothing is on screen
 * unless it is needed.
 */
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <wiiuse/wpad.h>
#include "game/hud.h"
#include "game/game.h"
#include "game/radio.h"
#include "game/notebook.h"
#include "game/story.h"
#include "game/listen.h"
#include "game/director.h"
#include "game/menu.h"
#include "game/guide.h"
#include "audio/vo.h"
#include "core/storage.h"
#include "audio/audio.h"
#include "input/input.h"
#include "gfx/gfx.h"
#include "gfx/text.h"
#include "palette_data.h"

static void ring(float cx, float cy, float r, float thick, GXColor c)
{
	const int seg = 20;
	mat_vcolor();
	vdesc_2d(false);
	gx_begin(GX_TRIANGLESTRIP, VF_2D, (seg + 1) * 2);
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
	gx_begin(GX_TRIANGLEFAN, VF_2D, seg + 2);
	GX_Position2f32(cx, cy);
	GX_Color4u8(c.r, c.g, c.b, c.a);
	for (int i = 0; i <= seg; i++) {
		float a = LL_TAU * i / seg;
		GX_Position2f32(cx + cosf(a) * r, cy + sinf(a) * r);
		GX_Color4u8(c.r, c.g, c.b, c.a / 3);
	}
	GX_End();
}

static void line2(float x0, float y0, float x1, float y1, GXColor c)
{
	mat_vcolor();
	vdesc_2d(false);
	gx_begin(GX_LINES, VF_2D, 2);
	GX_Position2f32(x0, y0);
	GX_Color4u8(c.r, c.g, c.b, c.a);
	GX_Position2f32(x1, y1);
	GX_Color4u8(c.r, c.g, c.b, c.a);
	GX_End();
}

static void centred(font_id f, float y, const char *s, GXColor c, float a)
{
	text_draw_soft(f, (G.ui_w - text_width(f, s, -1)) * 0.5f, y, s, -1, c, a);
}

/* ------------------------------------------------------------------------ */

static void draw_reticle(void)
{
	static float s_hover_a;
	int n;
	const interactable_t *ia = world_interactables(&n);
	bool hot = P.hover >= 0;
	s_hover_a += ((hot ? 1.0f : 0.0f) - s_hover_a) * 0.25f;
	if (P.mode == MODE_NOTEBOOK || P.mode == MODE_FINDER || P.mode == MODE_SLEEP || P.zoom > 0.3f) return;
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
			const char *verb = ia[P.hover].verb;
			if (ia[P.hover].kind == IA_LAMP) verb = S.lamp_on ? "Douse" : "Light";
			text_draw_soft(FONT_S, x + 16.0f, y + 6.0f, verb, -1, tc, s_hover_a);
		}
	}
}

/* Lines of a text with explicit breaks, each paragraph wrapped. */
static int layout(const char *s, float w, char lines[][160], int max)
{
	int count = 0;
	while (*s && count < max) {
		const char *nl = s;
		while (*nl && *nl != '\n') nl++;
		char buf[400];
		int plen = (int)(nl - s);
		if (plen > 399) plen = 399;
		for (int i = 0; i < plen; i++) buf[i] = s[i];
		buf[plen] = 0;
		int st[12], ln[12];
		int n = text_wrap(FONT_L, buf, w, st, ln, 12);
		for (int i = 0; i < n && count < max; i++, count++) {
			int l = ln[i] < 159 ? ln[i] : 159;
			for (int k = 0; k < l; k++) lines[count][k] = buf[st[i] + k];
			lines[count][l] = 0;
		}
		s = *nl ? nl + 1 : nl;
	}
	return count;
}

static void draw_text_card(void)
{
	if (P.read_fade < 0.01f || P.reading < 0) return;
	float a = P.read_fade;
	static char lines[14][160];
	float w = 490.0f;
	int n = layout(game_text(P.reading), w - 56.0f, lines, 14);
	const char *title = P.reading < NOTE_COUNT ? note_title(P.reading)
	                    : P.reading == TEXT_WEATHER ? "Weather station"
	                    : (P.reading > TEXT_SUPPLY ? "Supply box" : "Logbook");
	float lh = text_line_height(FONT_L) * 0.84f;
	float h = n * lh + 92.0f;
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
	text_draw(FONT_S, x + 28.0f, y + 32.0f, title, -1, gx_col(rgb_from(UI_INK), 0.45f * a));
	GXColor ink = gx_col(rgb_from(UI_INK), 0.92f * a);
	float ly = y + 56.0f + text_ascent(FONT_L);
	for (int i = 0; i < n; i++, ly += lh)
		text_draw(FONT_L, x + 28.0f, ly, lines[i], -1, ink);
}

static void draw_seated_hint(void)
{
	static float s_seated_t;
	if (P.mode != MODE_SIT || P.input_locked) {
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
		centred(FONT_S, 52.0f, "time passing", c, a);
		centred(FONT_L, 92.0f, clock, c, a);
	} else {
		centred(FONT_S, 452.0f, IN.nunchuk ? "Hold Z to let time pass  -  A to stand"
		                                   : "Hold the D-pad down to let time pass  -  A to stand", c, a);
	}
}

static void draw_day_card(void)
{
	float t = P.day_card;
	float a = saturatef((t - 0.6f) / 1.2f) * (1.0f - saturatef((t - 5.5f) / 1.5f));
	if (a < 0.01f) return;
	const day_def *d = day_info(S.day);
	char title[32];
	snprintf(title, sizeof(title), "Day %d", S.day);
	text_begin();
	GXColor c = gx_col(rgb_from(UI_SUBTITLE), a);
	centred(FONT_L, 116.0f, title, c, a);
	centred(FONT_L, 150.0f, d->title, c, a * 0.95f);
	centred(FONT_S, 178.0f, d->date, c, a * 0.85f);
}

static void draw_area_card(void)
{
	float t = S.area_card_t;
	float a = saturatef((t - 0.4f) / 1.0f) * (1.0f - saturatef((t - 4.5f) / 1.5f));
	if (a < 0.01f || S.area_card < 0) return;
	text_begin();
	centred(FONT_L, 70.0f, world_areas()[S.area_card].name, gx_col(rgb_from(UI_SUBTITLE), a * 0.95f), a);
}

static void draw_thought(void)
{
	if (!S.thought) return;
	float t = S.thought_t, len = 2.5f + 0.06f * (float)strlen(S.thought);
	float a = saturatef(t / 0.5f) * (1.0f - saturatef((t - len) / 1.0f));
	if (a < 0.01f || radio_in_call()) return;
	text_begin();
	centred(FONT_S, 430.0f, S.thought, gx_col(rgb_make(0.88f, 0.90f, 0.93f), a * 0.9f), a);
}

static void draw_hint(void)
{
	if (!S.hint) return;
	float hold = strchr(S.hint, '\n') ? 8.0f : 5.0f;          /* two lines stay longer */
	float t = S.hint_t, a = saturatef(t / 0.4f) * (1.0f - saturatef((t - hold) / 1.0f));
	if (a < 0.01f) return;
	text_begin();
	/* A newline splits it in two, right-aligned, the last line at the bottom. */
	const char *nl = strchr(S.hint, '\n');
	int len1 = nl ? (int)(nl - S.hint) : -1;
	float y = nl ? 452.0f - text_line_height(FONT_S) : 452.0f;
	GXColor c = gx_col(rgb_from(UI_SUBTITLE), a * 0.8f);
	text_draw_soft(FONT_S, G.ui_w - 40.0f - text_width(FONT_S, S.hint, len1), y, S.hint, len1, c, a);
	if (nl) text_draw_soft(FONT_S, G.ui_w - 40.0f - text_width(FONT_S, nl + 1, -1), 452.0f, nl + 1, -1, c, a);
}

/* Binoculars: a two-circle view with soft edges, faint pencil labels on known places. */
static void draw_binoculars(void)
{
	float z = P.zoom;
	if (z < 0.02f) return;
	const int NX = 32, NY = 24;
	float W = G.ui_w, H = G.ui_h;
	float r = H * (0.62f - 0.18f * z), cx0 = W * 0.5f - H * 0.2f, cx1 = W * 0.5f + H * 0.2f, cy = H * 0.5f;
	mat_vcolor();
	gfx_blend_alpha();
	vdesc_2d(false);
	for (int j = 0; j < NY; j++) {
		gx_begin(GX_TRIANGLESTRIP, VF_2D, (NX + 1) * 2);
		for (int i = 0; i <= NX; i++)
			for (int k = 0; k < 2; k++) {
				float x = W * i / NX, y = H * (j + k) / NY;
				float d0 = sqrtf((x - cx0) * (x - cx0) + (y - cy) * (y - cy));
				float d1 = sqrtf((x - cx1) * (x - cx1) + (y - cy) * (y - cy));
				float d = fminf(d0, d1) / r;
				u8 al = (u8)(smoothstepf(0.82f, 1.02f, d) * 245.0f * z);
				GX_Position2f32(x, y);
				GX_Color4u8(8, 9, 12, al);
			}
		GX_End();
	}
	if (z < 0.8f) return;
	/* Labels for the places Theo knows, in view and not too close. */
	float la = (z - 0.8f) / 0.2f;
	text_begin();
	for (int i = 0; i < AREA_COUNT; i++) {
		const world_place *p = &world_areas()[i];
		v3 w = v3_make(p->x, world_height(p->x, p->z) + 6.0f, p->z);
		float dist = v3_len(v3_sub(w, G.eye));
		float sx, sy;
		if (dist < 40.0f || !gfx_project(w, &sx, &sy)) continue;
		if (sx < 40 || sx > W - 160 || sy < 40 || sy > H - 60) continue;
		GXColor c = gx_col(rgb_make(0.95f, 0.93f, 0.88f), 0.75f * la);
		line2(sx, sy, sx, sy - 18, c);
		char label[64];
		snprintf(label, sizeof(label), "%s  %d m", p->name, (int)(dist / 10.0f + 0.5f) * 10);
		text_begin();
		text_draw_soft(FONT_S, sx + 4, sy - 22, label, -1, c, 0.6f * la);
	}
}

/* The fire finder: a sighting ring, turned with you, bearings to the six areas. */
static void draw_finder(void)
{
	float a = P.finder_fade;
	if (a < 0.01f) return;
	float cx = G.ui_w * 0.5f, cy = G.ui_h * 0.52f, R = 168.0f;
	mat_vcolor();
	gfx_blend_alpha();
	ui_rect(0, 0, G.ui_w, G.ui_h, (GXColor){ 10, 10, 14, (u8)(60 * a) });
	/* Paper disc. */
	vdesc_2d(false);
	rgbf pc = rgb_from(UI_PAPER);
	GXColor pg = gx_col(pc, 0.93f * a);
	gx_begin(GX_TRIANGLEFAN, VF_2D, 42);
	GX_Position2f32(cx, cy);
	GX_Color4u8(pg.r, pg.g, pg.b, pg.a);
	for (int i = 0; i <= 40; i++) {
		float t = LL_TAU * i / 40.0f;
		GX_Position2f32(cx + cosf(t) * (R + 28), cy + sinf(t) * (R + 28));
		GX_Color4u8(pg.r, pg.g, pg.b, pg.a);
	}
	GX_End();
	GXColor ink = gx_col(rgb_from(UI_INK), 0.85f * a), faint = gx_col(rgb_from(UI_INK), 0.4f * a);
	float yaw = P.yaw;
	/* Degree ring: the current heading is always straight up. */
	for (int d = 0; d < 360; d += 5) {
		float ang = DEG2RAD((float)d) - yaw;
		float s = sinf(ang), c = -cosf(ang);
		float r0 = d % 30 == 0 ? R - 14.0f : (d % 10 == 0 ? R - 9.0f : R - 5.0f);
		line2(cx + s * r0, cy + c * r0, cx + s * R, cy + c * R, d % 10 == 0 ? ink : faint);
	}
	text_begin();
	static const char *CARD[4] = { "N", "E", "S", "W" };
	for (int k = 0; k < 4; k++) {
		float ang = DEG2RAD(90.0f * k) - yaw;
		float tx = cx + sinf(ang) * (R + 14), ty = cy - cosf(ang) * (R + 14);
		text_draw(FONT_L, tx - text_width(FONT_L, CARD[k], -1) * 0.5f, ty + 9, CARD[k], -1, ink);
	}
	/* The six areas, at their bearings from the tower, nearer ones closer in. */
	for (int i = 0; i < AREA_COUNT; i++) {
		const world_place *p = &world_areas()[i];
		float dx = p->x - TOWER_X, dz = p->z - TOWER_Z;
		float dist = sqrtf(dx * dx + dz * dz);
		if (dist < 30.0f) continue;
		float b = atan2f(dx, -dz), ang = b - yaw;
		float rr = R * (0.35f + 0.5f * saturatef(dist / 260.0f));
		float px = cx + sinf(ang) * rr, py = cy - cosf(ang) * rr;
		dot(px, py, 3.0f, ink);
		char label[48];
		int deg = ((int)(RAD2DEG(b) + 360.5f)) % 360;
		snprintf(label, sizeof(label), "%s %03d", p->name, deg);
		text_begin();
		text_draw(FONT_S, px + 6, py + 5, label, -1, ink);
	}
	/* Sight line and the reading. */
	line2(cx, cy, cx, cy - R, gx_col(rgb_make(0.6f, 0.2f, 0.15f), 0.9f * a));
	char rd[32];
	snprintf(rd, sizeof(rd), "Bearing %03d", ((int)(RAD2DEG(yaw) + 360.5f)) % 360);
	text_begin();
	centred(FONT_L, cy + R + 64.0f, rd, gx_col(rgb_from(UI_SUBTITLE), a), a);
}

static void draw_fade(void)
{
	mat_vcolor();
	gfx_blend_alpha();
	if (WX.flash > 0.01f)
		ui_rect(0, 0, G.ui_w, G.ui_h, (GXColor){ 235, 240, 255, (u8)(70 * WX.flash) });
	if (S.fade > 0.003f)
		ui_rect(0, 0, G.ui_w, G.ui_h, (GXColor){ 0, 0, 0, (u8)(255 * saturatef(S.fade)) });
}

/* Through the gate: the closing card, then the credits. */
static void draw_ending(void)
{
	if (P.mode != MODE_ENDING) return;
	float t = S.ending_t;
	GXColor paper = gx_col(rgb_from(UI_SUBTITLE), 1.0f);
	const char *card[ENDING_LINES];
	int n = game_ending_card(card);
	float credits_at = game_credits_at();
	text_begin();
	if (t < credits_at) {
		for (int i = 0; i < n; i++) {
			float a = saturatef((t - 3.0f - i * 1.8f) / 1.2f) * (1.0f - saturatef((t - credits_at + 1.5f) / 1.2f));
			if (a > 0.01f) centred(FONT_S, 160.0f + i * 32.0f, card[i], gx_col(rgb_from(UI_SUBTITLE), 0.9f * a), a);
		}
		return;
	}
	const char *CREDITS[] = {
		"Lone Lookout", "", "A small game about listening.", "", "Dot, Theo, Augie, and the Harrow crew",
		!vo_available()  ? "were played by a synthesiser that only knows how to murmur."
		: vo_elevenlabs() ? "were voiced with ElevenLabs text to speech, from your SD card."
		                  : "were played by the voices on your SD card, and a synthesiser that murmurs.",
		"", "Thank you for keeping watch.",
	};
	float c = t - credits_at;
	for (int i = 0; i < (int)(sizeof(CREDITS) / sizeof(CREDITS[0])); i++) {
		float a = saturatef((c - i * 0.5f) / 1.5f);
		if (a > 0.01f && CREDITS[i][0])
			centred(i ? FONT_S : FONT_L, 150.0f + i * 30.0f, CREDITS[i], gx_col(rgb_from(UI_SUBTITLE), 0.92f * a), a);
	}
	float pa = saturatef((c - 4.0f) / 1.0f) * (0.6f + 0.25f * sinf(t * 2.0f));
	if (pa > 0.01f) centred(FONT_S, 430.0f, "A: one more look", paper, pa);
}

static void draw_dev_overlay(void)
{
#if defined(LL_SHOT_TOUR) && !defined(LL_PERF)
	return;
#elif !defined(LL_INPUT_SCRIPT) && !defined(LL_MENU_TEST) && !defined(LL_PERF)
	if (!(in_held(WPAD_BUTTON_1) && in_held(WPAD_BUTTON_2))) return;
#endif
	char line[200];
	text_begin();
	GXColor c = { 255, 250, 235, 230 };
	snprintf(line, sizeof(line), "%.1f ms  %.0f fps  day %d %02d:%02d  pos %.1f %.1f %.1f  yaw %.0f pitch %.0f",
	         G.work_ms, G.refresh_hz / G.vsyncs, S.day, (int)DAY.hour, (int)((DAY.hour - (int)DAY.hour) * 60),
	         P.pos.x, P.pos.y, P.pos.z, RAD2DEG(P.yaw), RAD2DEG(P.pitch));
	text_draw_soft(FONT_S, 20, 30, line, -1, c, 1.0f);
	snprintf(line, sizeof(line), "mode %d hover %d radio %s surf %d area %d  chunks %d trees %d  wx c%.2f s%.2f r%.2f h%.2f",
	         (int)P.mode, P.hover, radio_busy() ? (radio_in_call() ? "call" : "ring") : "idle", (int)P.surface, S.area,
	         G.stat_chunks, G.stat_trees, WX.cloud, WX.storm, WX.rain, WX.haze);
	text_draw_soft(FONT_S, 20, 54, line, -1, c, 1.0f);
	u32 under, blocks;
	float mix_ms;
	audio_stats(&under, &blocks, &mix_ms);
	snprintf(line, sizeof(line), "audio: %u blocks, %u underruns, mix %.2f ms of 16  music %s %.0f%% of play  sounds %d",
	         (unsigned)blocks, (unsigned)under, mix_ms, audio_music_playing() ? "on" : "off",
	         director_music_share() * 100.0f, sounds_logged());
	text_draw_soft(FONT_S, 20, 78, line, -1, c, 1.0f);
	char vo[96];
	vo_debug(vo, sizeof(vo));
	snprintf(line, sizeof(line), "sd: %s   %s   options: sens %.1f box %d compass %d subs %d mono %d vol %d/%d/%d",
	         storage_ok() ? "mounted" : "none", vo, OPT.look_sens, OPT.comfort, OPT.compass, OPT.subs_large, OPT.mono,
	         OPT.vol_music, OPT.vol_ambience, OPT.vol_voice);
	text_draw_soft(FONT_S, 20, 102, line, -1, c, 1.0f);
	u32 total = 0;
	for (int i = 0; i < TRI_CATS; i++) total += G.tris_last[i];
	static const char *const FORMATS[] = { "NTSC", "PAL", "MPAL", "debug", "debug PAL", "EuRGB60" };
	u32 tv = G.rmode->viTVMode;
	snprintf(line, sizeof(line), "tris %.1fk (peak %.1fk)  ground %.1f  trees %.1f  tufts %.1f  props %.1f  sky %.1f  ui %.1f",
	         total / 1000.0f, G.tris_peak / 1000.0f, G.tris_last[TRI_TERRAIN] / 1000.0f, G.tris_last[TRI_TREES] / 1000.0f,
	         G.tris_last[TRI_TUFTS] / 1000.0f, G.tris_last[TRI_PROPS] / 1000.0f, G.tris_last[TRI_SKY] / 1000.0f,
	         G.tris_last[TRI_UI] / 1000.0f);
	text_draw_soft(FONT_S, 20, 126, line, -1, c, 1.0f);
	struct mallinfo mi = mallinfo();
	snprintf(line, sizeof(line), "heap %u KB  nunchuk %d  video: %s %ux%u%s (efb %u) %.2f Hz %s",
	         (unsigned)(mi.uordblks / 1024), IN.nunchuk, (tv >> 2) < 6 ? FORMATS[tv >> 2] : "?", G.rmode->fbWidth,
	         G.rmode->xfbHeight, (tv & 3) == VI_PROGRESSIVE ? "p" : "i", G.rmode->efbHeight, G.refresh_hz,
	         G.widescreen ? "16:9" : "4:3");
	text_draw_soft(FONT_S, 20, 150, line, -1, c, 1.0f);
}

/* The compass (an option): the heading, pencilled at the top of the screen. */
static void draw_compass(void)
{
	if (!OPT.compass || (P.mode != MODE_WALK && P.mode != MODE_SIT) || P.zoom > 0.5f) return;
	static const char *POINTS[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
	int deg = ((int)(RAD2DEG(P.yaw) + 360.5f)) % 360;
	char s[16];
	snprintf(s, sizeof(s), "%s  %03d", POINTS[((deg + 22) / 45) % 8], deg);
	text_begin();
	centred(FONT_S, 30.0f, s, gx_col(rgb_from(UI_SUBTITLE), 0.75f), 0.75f);
}

static void draw_field_hud(void)
{
	if (menu_active() || (P.mode != MODE_WALK && P.mode != MODE_SIT) || P.zoom > 0.1f ||
	    radio_in_call() || P.day_card < 7.0f || listen_amount() > 0.3f) return;
	GXColor paper = gx_col(rgb_from(UI_SUBTITLE), 0.9f);
	float dist, angle;
	if (guide_bearing(P.pos, P.yaw, &dist, &angle)) {
		guide_place p = guide_get(GUIDE.pin);
		char line[96];
		const char *arrow = fabsf(angle) < DEG2RAD(14.0f) ? "^" : angle < 0 ? "<" : ">";
		if (dist < p.radius) snprintf(line, sizeof(line), "%s  You are here", p.name);
		else snprintf(line, sizeof(line), "%s  %s  %.0f m", arrow, p.name, dist);
		text_begin();
		centred(FONT_S, 55, line, paper, 0.9f);
	}
	if (S.notice[0] && S.notice_t < 6.0f) {
		float a = saturatef(S.notice_t * 3.0f) * saturatef(6.0f - S.notice_t);
		float w = 300, x = G.ui_w - w - 26, y = 82;
		mat_vcolor();
		gfx_blend_alpha();
		ui_rect(x, y, w, 82, (GXColor){ 21, 30, 32, (u8)(215 * a) });
		ui_rect(x, y, 3, 82, (GXColor){ 220, 173, 107, (u8)(255 * a) });
		text_begin();
		text_draw(FONT_S, x + 15, y + 25, S.notice, -1, gx_col(rgb_from(UI_SPEAKER_DOT), a));
		int st[2], ln[2];
		int n = text_wrap(FONT_S, S.notice_detail, w - 30, st, ln, 2);
		for (int i = 0; i < n; i++) text_draw(FONT_S, x + 15, y + 49 + i * 21, S.notice_detail + st[i], ln[i],
		                                     gx_col(rgb_from(UI_SUBTITLE), a));
	}
	if (S.saved_t < 4.0f) {
		text_begin();
		text_draw_soft(FONT_S, 28, 35, S.save_failed ? "Could not save to the SD card" : "Season saved", -1, paper, saturatef(4.0f - S.saved_t));
	}
	if (S.day == 1 && P.day_card < 26.0f && !radio_busy()) {
		text_begin();
		const char *help = IN.nunchuk ? "Stick: walk   Z: stride   A: use   B: radio   Down: notebook"
		                               : "D-pad: walk / turn   A: use   B: radio   2: notebook";
		centred(FONT_S, 465, help, paper, saturatef(26.0f - P.day_card));
	}
}

void hud_draw(void)
{
	gfx_ortho();
	gfx_depth(false, false);
	if (APP == APP_PLAY) {
		draw_binoculars();
		draw_day_card();
		draw_area_card();
		if (!menu_active()) draw_reticle();
		listen_draw();
		draw_seated_hint();
		draw_thought();
		draw_hint();
		draw_compass();
		draw_field_hud();
		radio_draw();
		draw_finder();
		notebook_draw();
		draw_text_card();
		draw_fade();
		draw_ending();
	}
	menu_draw();
#ifdef LL_MENU_TEST
	draw_dev_overlay();
#else
	if (APP == APP_PLAY) draw_dev_overlay();
#endif
	gfx_blend_none();
}
