/*
 * listen.c -- the sound log's twelve sounds in the world, Listen, and
 * writing a sound down.
 *
 * Each sound is a point (the woodpecker's snag, the porch chimes), a zone
 * you stand inside (grasshoppers in the meadow, wind in the burn's snags),
 * a line (the creek) or an event that rings for a moment (the singing
 * stair, an elk's bugle). Gain falls off with distance and is muffled
 * indoors; pan follows where it is relative to the way Theo faces.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <wiiuse/wpad.h>
#include "game/listen.h"
#include "game/game.h"
#include "game/story.h"
#include "game/radio.h"
#include "game/playtest.h"
#include "audio/audio.h"
#include "input/input.h"
#include "gfx/gfx.h"
#include "gfx/text.h"
#include "world/sites.h"
#include "palette_data.h"

#define LOG_HOLD 3.0f            /* seconds on a sound to write it down */
#define EVENT_HOLD 1.5f          /* ...for one that only rings for a moment */
#define EVENT_WINDOW 6.0f
#define NOTHING_HOLD 10.0f
#define FOCUS_DEG 35.0f
#define HEARD 0.03f              /* quieter than this gets no label */

#define EM "\xE2\x80\x94"
static const struct { const char *name, *note; } SOUNDS[SND_COUNT] = {
	[SND_STAIR] = { "The third stair", "The third stair from the top. E-flat, a little flat. Not F, Augie." },
	[SND_CREEK] = { "Creek over stones", "Creek over stones. Never the same twice, never different either." },
	[SND_GRASSHOPPERS] = { "Grasshoppers", "Grasshoppers at midday, like a room of typewriters." },
	[SND_ELK] = { "An elk, bugling", "A bull elk. Starts as a grunt, ends as a rusty gate singing." },
	[SND_SANDPIPER] = { "Sandpiper", "Sandpiper on the shore. Peet-weet. Peet-weet. Bobbing." },
	[SND_FROGS] = { "Chorus frogs", "Chorus frogs: a thumb run down a comb. A hundred combs." },
	[SND_PIKA] = { "Pika", "A pika in the scree. One small, indignant eep." },
	[SND_SNAGS] = { "Wind in the snags", "Wind in the snags. The burn breathing through its teeth." },
	[SND_WOODPECKER] = { "Woodpecker", "Woodpecker on a dead tree. Morse code with no message." },
	[SND_CHIMES] = { "Spoon chimes", "Augie's spoon chimes. Out of tune. Of course they are." },
	[SND_TIN_RAIN] = { "Rain on the tin roof", "Rain on the tin roof, the first in seven weeks. Every drop its own note." },
	[SND_NOTHING] = { "Almost nothing", "Almost nothing. Not nothing. Very small things." },
};

const char *sound_name(int id) { return id >= 0 && id < SND_COUNT ? SOUNDS[id].name : ""; }
const char *sound_note(int id) { return id >= 0 && id < SND_COUNT ? SOUNDS[id].note : ""; }

static struct {
	float amount;
	bool held, force;
	int focus;
	float hold_t;
	float heard[SND_COUNT];      /* how loud each is here (labels and focus) */
	float bed[SND_COUNT];        /* what the mixer should play */
	float pan[SND_COUNT];
	v3 pos[SND_COUNT];
	bool around[SND_COUNT];      /* all around (standing in its zone) */
	float event_t[SND_COUNT];
	v3 event_pos[SND_COUNT];
	float nothing_t;
} L;

void listen_init(void)
{
	memset(&L, 0, sizeof(L));
	L.focus = -1;
	for (int k = 0; k < SND_COUNT; k++) L.event_t[k] = 1e9f;
}

float listen_amount(void) { return L.amount; }

void listen_event(int sound, v3 pos)
{
	if (sound < 0 || sound >= SND_COUNT) return;
	L.event_t[sound] = 0.0f;
	L.event_pos[sound] = pos;
}

int sounds_logged(void)
{
	int n = 0;
	for (int k = 0; k < SND_COUNT; k++) n += S.sound_day[k] != 0;
	return n;
}

/* ------------------------------------------------------------------------ */

static v3 s_eye;

static float point(int id, v3 p, float radius, float vol)
{
	L.pos[id] = p;
	float d = v3_len(v3_sub(p, s_eye));
	float k = saturatef(1.0f - d / radius);
	return vol * k * sqrtf(k);
}

/* A zone: all around inside it, from its edge outside, fading over 'fade' metres. */
static float zone(int id, float cx, float cz, float r, float fade, float vol)
{
	float dx = s_eye.x - cx, dz = s_eye.z - cz, d = sqrtf(dx * dx + dz * dz) + 1e-3f;
	if (d < r) {
		L.around[id] = true;
		v3 f = v3_make(sinf(P.yaw), 0.0f, -cosf(P.yaw));
		float x = s_eye.x + f.x * 6.0f, z = s_eye.z + f.z * 6.0f;
		L.pos[id] = v3_make(x, world_height(x, z) + 0.3f, z);
		return vol;
	}
	float x = cx + dx / d * r, z = cz + dz / d * r;
	L.pos[id] = v3_make(x, world_height(x, z) + 0.5f, z);
	return vol * (1.0f - saturatef((d - r) / fade));
}

static v3 creek_nearest(void)
{
	int n;
	const float (*c)[2] = world_creek(&n);
	v3 best = v3_make(c[0][0], 0, c[0][1]);
	float bd = 1e18f;
	for (int i = 0; i + 1 < n; i++) {
		float ax = c[i][0], az = c[i][1], bx = c[i + 1][0], bz = c[i + 1][1];
		float ex = bx - ax, ez = bz - az, len2 = ex * ex + ez * ez + 1e-6f;
		float t = saturatef(((s_eye.x - ax) * ex + (s_eye.z - az) * ez) / len2);
		float x = ax + ex * t, z = az + ez * t;
		float d = (x - s_eye.x) * (x - s_eye.x) + (z - s_eye.z) * (z - s_eye.z);
		if (d < bd) {
			bd = d;
			best = v3_make(x, 0, z);
		}
	}
	best.y = world_height(best.x, best.z) + 0.2f;
	return best;
}

static bool between(float h, float from, float to)
{
	return from <= to ? (h >= from && h < to) : (h >= from || h < to);
}

/* Where each sound is and how loud, this frame. */
static void place_sounds(float dt)
{
	float h = DAY.hour, wind = WX.wind;
	int day = S.day;
	bool indoor = world_indoor(P.pos) != INDOOR_NONE, in_cab = world_indoor(P.pos) == INDOOR_CAB;
	const world_place *A = world_areas();
	memset(L.heard, 0, sizeof(L.heard));
	memset(L.bed, 0, sizeof(L.bed));
	memset(L.around, 0, sizeof(L.around));

	L.bed[SND_CREEK] = point(SND_CREEK, creek_nearest(), 42.0f, 0.9f);
	if (between(h, 10.5f, 16.5f) && day <= 9 && WX.rain < 0.1f)
		L.bed[SND_GRASSHOPPERS] = zone(SND_GRASSHOPPERS, A[AREA_MEADOW].x, A[AREA_MEADOW].z, 52.0f, 25.0f, 0.75f);
	{
		float sx = world_dock_x0() + 3.0f, sz = DOCK_Z - 5.0f;
		if (between(h, 5.5f, 10.5f) && day <= 10)
			L.bed[SND_SANDPIPER] = point(SND_SANDPIPER, v3_make(sx, DOCK_DECK_Y, sz), 80.0f, 0.85f);
		if (between(h, 20.5f, 4.5f) && day <= 9)
			L.bed[SND_FROGS] = point(SND_FROGS, v3_make(A[AREA_TARN].x - 10.0f, DOCK_DECK_Y - 0.3f, A[AREA_TARN].z - 12.0f),
			                         120.0f, 0.9f);
	}
	if (between(h, 8.0f, 18.0f))
		L.bed[SND_PIKA] = point(SND_PIKA, v3_make(CAIRN_X - 9.0f, world_height(CAIRN_X - 9.0f, CAIRN_Z - 6.0f) + 0.3f,
		                                          CAIRN_Z - 6.0f), 55.0f, 0.85f);
	if (wind > 0.25f)
		L.bed[SND_SNAGS] = zone(SND_SNAGS, A[AREA_BURN].x, A[AREA_BURN].z, 58.0f, 30.0f, clampf(wind, 0.3f, 1.0f));
	if (between(h, 6.0f, 11.0f))
		L.bed[SND_WOODPECKER] = point(SND_WOODPECKER, v3_make(TAG_SNAG_X, world_height(TAG_SNAG_X, TAG_SNAG_Z) + 5.0f,
		                                                      TAG_SNAG_Z), 150.0f, 0.9f);
	{
		int ci = world_find_interactable(IA_CHIMES, 0);
		int n;
		v3 cp = world_interactables(&n)[ci].pos;
		if (wind > 0.4f) L.bed[SND_CHIMES] = point(SND_CHIMES, cp, 45.0f, 0.9f);
		else L.pos[SND_CHIMES] = cp;
	}
	if (day == NUM_DAYS && WX.rain > 0.3f) {
		v3 roof = v3_make(TOWER_X, world_pad_height() + TOWER_DECK_Y + 2.8f, TOWER_Z);
		L.pos[SND_TIN_RAIN] = roof;
		float d = v3_len(v3_sub(roof, s_eye));
		L.bed[SND_TIN_RAIN] = in_cab ? 1.0f : 0.4f * saturatef(1.0f - (d - 3.0f) / 6.0f);
	}
	if (between(h, 21.0f, 4.5f) && wind < 0.35f)
		L.bed[SND_NOTHING] = zone(SND_NOTHING, A[AREA_RIDGE].x, A[AREA_RIDGE].z, A[AREA_RIDGE].radius + 8.0f, 6.0f, 0.4f);

	/* Muffled indoors (except the roof over your head). */
	for (int k = 0; k < SND_COUNT; k++) {
		if (indoor && k != SND_TIN_RAIN) L.bed[k] *= 0.35f;
		L.heard[k] = L.bed[k];
	}

	/* The one-shots: audible (and loggable) for a few seconds. */
	static const float EV_R[SND_COUNT] = { [SND_STAIR] = 20.0f, [SND_ELK] = 700.0f, [SND_CHIMES] = 45.0f };
	for (int k = 0; k < SND_COUNT; k++) {
		if (L.event_t[k] > 1e8f) continue;
		L.event_t[k] += dt;
		if (L.event_t[k] < EVENT_WINDOW) {
			v3 keep = L.pos[k];
			float g = point(k, L.event_pos[k], EV_R[k] > 0.0f ? EV_R[k] : 40.0f, 1.0f) * (indoor ? 0.5f : 1.0f);
			if (g > L.heard[k]) L.heard[k] = g;
			else L.pos[k] = keep;
		}
	}

	/* Pan: where it is relative to the way Theo faces. */
	float cy = cosf(P.yaw), sy = sinf(P.yaw);
	for (int k = 0; k < SND_COUNT; k++) {
		float dx = L.pos[k].x - s_eye.x, dz = L.pos[k].z - s_eye.z, d = sqrtf(dx * dx + dz * dz) + 1e-3f;
		L.pan[k] = L.around[k] ? 0.0f : clampf((dx * cy + dz * sy) / d, -1.0f, 1.0f) * 0.8f;
		audio_set_emitter(k, L.bed[k], L.pan[k]);
	}
}

static void write_down(int id, const char *thought)
{
	if (S.sound_day[id]) return;
	S.sound_day[id] = (u8)S.day;
	game_notice("A sound worth keeping", SOUNDS[id].name);
	audio_ui(UI_PENCIL);
	playtest_event("sound", "%s", SOUNDS[id].name);
	story_raise(FLAG_SOUND_LOGGED);
	static char line[96];
	snprintf(line, sizeof(line), "%s. Written down.", SOUNDS[id].name);
	game_think(thought ? thought : line);
}

void listen_update(float dt)
{
	s_eye = game_eye();
	place_sounds(dt);

	bool can = (P.mode == MODE_WALK || P.mode == MODE_SIT) && !radio_wants_pointer() && P.zoom < 0.1f;
	bool button = IN.nunchuk ? in_held(WPAD_NUNCHUK_BUTTON_C) : in_held(WPAD_BUTTON_MINUS);
	bool was = L.held;
	L.held = L.force || (can && !P.input_locked && button);
	if (L.held && !was && !L.force) playtest_event("listen", NULL);
	L.amount = approachf(L.amount, L.held ? 1.0f : 0.0f, L.held ? 3.0f : 5.0f, dt);

	/* Focus: the loudest sound near where the reticle points. */
	int focus = -1;
	if (L.held) {
		v3 dir = game_reticle_dir();
		float best = 0.0f;
		for (int k = 0; k < SND_COUNT; k++) {
			if (L.heard[k] < HEARD) continue;
			float ang = 0.0f;
			if (!L.around[k]) {
				v3 to = v3_norm(v3_sub(L.pos[k], s_eye));
				ang = RAD2DEG(acosf(clampf(v3_dot(to, dir), -1.0f, 1.0f)));
			}
			if (ang > FOCUS_DEG) continue;
			float score = L.heard[k] * (1.0f - 0.8f * ang / FOCUS_DEG);
			if (score > best) {
				best = score;
				focus = k;
			}
		}
	}
	L.hold_t = focus >= 0 && focus == L.focus ? L.hold_t + dt : 0.0f;
	L.focus = focus;
	audio_set_listen_focus(focus);

	if (focus >= 0 && focus != SND_NOTHING && !S.sound_day[focus]) {
		bool event = focus == SND_STAIR || focus == SND_ELK || (focus == SND_CHIMES && L.bed[SND_CHIMES] < HEARD);
		if (L.hold_t >= (event ? EVENT_HOLD : LOG_HOLD)) write_down(focus, NULL);
	}

	/* S12: ten seconds of listening to almost nothing on the ridge at night. */
	bool others = false;
	for (int k = 0; k < SND_COUNT; k++)
		if (k != SND_NOTHING && L.heard[k] > 0.08f) others = true;
	L.nothing_t = L.held && L.heard[SND_NOTHING] > 0.2f && !others ? L.nothing_t + dt : 0.0f;
	if (L.nothing_t >= NOTHING_HOLD) write_down(SND_NOTHING, "Not nothing. Very small things.");
}

/* ------------------------------------------------------------------------ */

static void underline(float x, float y, float w, float frac, GXColor c)
{
	mat_vcolor();
	gfx_blend_alpha();
	ui_rect(x, y, w * frac, 2.0f, c);
	GXColor faint = c;
	faint.a /= 4;
	ui_rect(x + w * frac, y, w * (1.0f - frac), 2.0f, faint);
}

void listen_draw(void)
{
	float a = L.amount;
	if (a < 0.02f) return;
	/* The edges darken a touch: attention narrowing. */
	mat_vcolor();
	gfx_blend_alpha();
	ui_rect(0, 0, G.ui_w, 26.0f * a, (GXColor){ 8, 9, 12, (u8)(90 * a) });
	ui_rect(0, G.ui_h - 26.0f * a, G.ui_w, 26.0f * a, (GXColor){ 8, 9, 12, (u8)(90 * a) });

	int around_slot = 0;
	for (int k = 0; k < SND_COUNT; k++) {
		if (L.heard[k] < HEARD) continue;
		float sx, sy;
		bool on;
		if (L.around[k]) {
			sx = G.ui_w * 0.5f + (around_slot++ - 0.5f) * 150.0f;
			sy = G.ui_h * 0.7f;
			on = true;
		} else {
			on = gfx_project(L.pos[k], &sx, &sy);
		}
		const char *name = SOUNDS[k].name;
		float tw = text_width(FONT_S, name, -1);
		bool edge = false;
		if (!on || sx < 30.0f || sx > G.ui_w - 30.0f || sy < 40.0f || sy > G.ui_h - 40.0f) {
			/* Off to one side: pinned to the edge it's on. */
			edge = true;
			sx = L.pan[k] < 0.0f ? 40.0f : G.ui_w - 40.0f - tw;
			sy = clampf(on ? sy : G.ui_h * 0.5f, 60.0f, G.ui_h - 60.0f);
		} else {
			sx -= tw * 0.5f;
		}
		bool focus = k == L.focus;
		float la = a * saturatef(L.heard[k] * 5.0f) * (focus ? 1.0f : 0.6f);
		GXColor c = gx_col(rgb_from(UI_SUBTITLE), la);
		text_begin();
		if (edge) {
			const char *arrow = L.pan[k] < 0.0f ? "<" : ">";
			text_draw_soft(FONT_S, L.pan[k] < 0.0f ? sx - 18.0f : sx + tw + 8.0f, sy, arrow, -1, c, la);
		}
		text_draw_soft(FONT_S, sx, sy, name, -1, c, la);
		if (S.sound_day[k]) {
			GXColor tick = gx_col(rgb_make(0.85f, 0.55f, 0.45f), la);
			text_draw_soft(FONT_S, sx + tw + 6.0f, sy, "*", -1, tick, la);
		} else if (focus) {
			float need = k == SND_NOTHING ? NOTHING_HOLD : ((k == SND_STAIR || k == SND_ELK) ? EVENT_HOLD : LOG_HOLD);
			float t = k == SND_NOTHING ? L.nothing_t : L.hold_t;
			underline(sx, sy + 6.0f, tw, saturatef(t / need), c);
		}
	}
}

void listen_debug_force(bool on) { L.force = on; }
