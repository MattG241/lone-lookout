/*
 * game.c -- walking, looking, sitting, reading, sleeping, and the small
 * rituals of the job: the lamp, the kettle, the logbook, the fire finder,
 * the binoculars and the notebook.
 *
 * Look uses the "comfort box" from DESIGN.md section 5: the pointer moves a
 * soft reticle freely in the middle of the screen; only near the edges does
 * the view turn, and losing the sensor bar eases turning to a stop.
 */
#include <math.h>
#include <string.h>
#include <wiiuse/wpad.h>
#include "game/game.h"
#include "game/radio.h"
#include "game/notebook.h"
#include "game/story.h"
#include "game/listen.h"
#include "game/director.h"
#include "game/save.h"
#include "audio/audio.h"
#include "input/input.h"
#include "gfx/gfx.h"
#include "gfx/foliage.h"
#include "gfx/water.h"
#include "world/sites.h"

#define EYE_HEIGHT 1.62f
#define WALK_SPEED 2.2f
#define STRIDE_SPEED 3.4f
/* The comfort box (small, medium, large): inside it the pointer moves the reticle only. */
static const float BOX_X[3] = { 0.40f, 0.55f, 0.68f }, BOX_Y[3] = { 0.36f, 0.50f, 0.62f };
#define MAX_YAW_RATE DEG2RAD(70.0f)
#define MAX_PITCH_RATE DEG2RAD(45.0f)
#define PITCH_LIMIT DEG2RAD(60.0f)
#define REACH 2.6f
#define STEP_LENGTH 0.72f
#define FOV_EYE 58.0f
#define FOV_BINOCULARS 16.0f
#define SLEEP_FROM 19.0f
#define SINGING_TREAD (3 * STAIR_STEPS + STAIR_STEPS - 3)   /* third from the top */
#define DAY_POSTCARD 9                                      /* Last Season */
#define DAY_CLOSING 10                                      /* First Frost: the closing chores */

player_t P;
story_t S;
options_t OPT = {
	.look_sens = 1.0f, .comfort = 1, .snap_turn = true, .head_sway = true, .walk_bob = true,
	.hearing = true, .vol_music = 8, .vol_ambience = 8, .vol_voice = 8,
};

/* ------------------------------------------------------------------------ */
/* Texts                                                                    */
/* ------------------------------------------------------------------------ */

#define EM "\xE2\x80\x94"
static const char *NOTES[NOTE_COUNT] = {
	[NOTE_WELCOME] = "To whoever's next: welcome to Kestrel. Kettle sticks " EM " hit it on the left. "
	                 "Radio's on 4. Dot's bark is worse than her bite. Her bite is also pretty bad.\n" EM " A.P.",
	[NOTE_STAIR] = "Third stair from the top sings. Always figured F.\nSay good morning to it.\n" EM " A.P.",
	[NOTE_MAP] = "Under the cot, folded in four: a map of the district in Augie's hand, drawn on the back of "
	             "a supply form. Every trail, every spring, the good rocks to sit on.\n(Copied into your notebook.)",
	[NOTE_BIRDS] = "Birds seen from the catwalk, 22 summers:\nraven (always), Clark's nutcracker, varyed thrush, "
	               "mountain chickadee, nighthawk, rufous hummingbird (rude), golden eagle (once, 2011).",
	[NOTE_FENCE] = "Doe and twins most mornings. Don't name them.\n(I named them. Sorry.)",
	[NOTE_SPRING] = "Best water in the district. Rinse the cup. It's a shared cup.\nShared with me.",
	[NOTE_DOCK] = "Best sitting spot in the district. Thermos is clean.\nMostly.",
	[NOTE_SNAG] = "Aug 9 1998. Watched this one go from the tower. Took a week to stop smelling it.\nLook down.",
	[NOTE_BENCH] = "Carved deep into the backrest, the letters gone silver:\nSIT HERE AT SEVEN",
	[NOTE_FLOWERS] = "A book of pressed flowers. On the first page: Things that came back after '98.\n"
	                 "Fireweed. Lupine. Larkspur. Paintbrush. Yarrow. A doe, the third summer.",
	[NOTE_LETTER] = "To the District Ranger: I hereby resign my post at Kestrel, effective the end of this season.\n"
	                "(Crossed out, in different ink:) Changed my mind. Stayed 14 more. " EM " A.P., 2009",
	[NOTE_POSTCARD] = "Kestrel " EM " heard there's a piano man up there now. Settle it: is it F?\n" EM " Augie\n"
	                  "P.S. Tell Dot to go see her sister.",
};

static const char *TITLES[NOTE_COUNT] = {
	"Welcome to Kestrel", "The singing stair", "Augie's map", "Birds from the catwalk", "The drift fence",
	"The cold spring", "The tin on the dock", "The tag on the snag", "The bench", "Pressed flowers",
	"The drawer", "A postcard",
};

static const char *LOGBOOK[NUM_DAYS] = {
	"21 Aug. Walked up from the gate, 15:30. Radio check with Basin. Wind light, W. Visibility good. "
	"Nothing to report.\n" EM " T. Marsh",
	"22 Aug. Clear and warm. Walked the north loop. Doe and two fawns below the fence. Nothing to report.",
	"23 Aug. High cloud, breezy. Supply drop at the gate. Radio poor near the tarn. Nothing to report.",
	"25 Aug. Breezy. Walked Augie's round: rain gauge, spring, the cairn. Told Basin what the wind sounds like.",
	"28 Aug. Dry storm from 14:00. Lightning on Harrow Ridge. Smoke reported to Basin.",
	"30 Aug. Smoke from Harrow all day. Relayed for the crew from the ridge. Dot on her third night shift.",
	"3 Sep. Haze from fires to the south all day. Sun red at 19:00. Sat at the bench, as instructed.",
	"6 Sep. Cool, breezy. Elk in the meadow at dusk " EM " counted four bulls by ear. Nothing to report.",
	"10 Sep. Clear, cold. First aspens turning by the meadow. Basin is closing. Nothing to report.",
	"13 Sep. Frost on the meadow at 06:00. Closing list came up on the truck. Shutters by the 15th.",
	"15 Sep. Clouding over. Rain by noon " EM " the first in seven weeks. Last page.",
};

static const char *SUPPLY[NUM_DAYS + 1] = {
	[3] = "In the box: tinned peaches, batteries, and a paperback with the cover torn off.",
	[9] = "In the box: batteries, coffee, and a postcard with a lighthouse on it.",
	[10] = "In the box: a closing checklist in Dot's capitals. Shutters. Drain the cistern. "
	       "Leave the logbook for whoever's next.",
};

const char *game_text(int id)
{
	if (id >= 0 && id < NOTE_COUNT) return NOTES[id];
	if (id > TEXT_LOGBOOK && id <= TEXT_LOGBOOK + NUM_DAYS) return LOGBOOK[id - TEXT_LOGBOOK - 1];
	if (id > TEXT_SUPPLY && id <= TEXT_SUPPLY + NUM_DAYS && SUPPLY[id - TEXT_SUPPLY]) return SUPPLY[id - TEXT_SUPPLY];
	if (id == TEXT_WEATHER) return weather_readings(S.day, DAY.hour);
	return "";
}

const char *note_title(int id) { return (id >= 0 && id < NOTE_COUNT) ? TITLES[id] : ""; }

/* One-time thoughts. */
enum { TH_GATE, TH_WATER, TH_RIM, TH_EARLY, TH_LAST_NIGHT, TH_CHIMES, TH_COFFEE, TH_STAIR, TH_NOT_YET, TH_EMPTY, TH_COUNT };

void game_think(const char *text)
{
	S.thought = text;
	S.thought_t = 0.0f;
}

static void think_once(int id, const char *text)
{
	if (S.thoughts_said & (1u << id)) return;
	S.thoughts_said |= 1u << id;
	game_think(text);
}

static void hint(const char *text)
{
	S.hint = text;
	S.hint_t = 0.0f;
}

/* ------------------------------------------------------------------------ */

float game_season(void) { return day_info(S.day)->season; }

static void face(float yaw, float pitch)
{
	P.yaw = yaw;
	P.pitch = pitch;
	P.yaw_rate = P.pitch_rate = P.snap_left = 0.0f;
}

void game_new_day(int day)
{
	S.day = day < 1 ? 1 : (day > NUM_DAYS ? NUM_DAYS : day);
	S.lamp_on = false;
	S.kettle_t = -1.0f;
	S.kettle_today = false;
	S.obs_am = S.obs_pm = false;
	S.areas_today = 0;
	story_new_day(S.day);
	S.season_dirty = true;
	DAY.hour = 6.0f;
	/* Wake beside the cot, looking out of the east windows. */
	P.pos = v3_make(TOWER_X - 0.7f, world_pad_height() + TOWER_DECK_Y, TOWER_Z - 0.6f);
	face(BEARING(95.0f), DEG2RAD(-2.0f));
	P.mode = MODE_WALK;
	P.sit_blend = 0.0f;
	P.day_card = 0.0f;
	P.vel = v3_make(0, 0, 0);
}

void game_init(void)
{
	memset(&P, 0, sizeof(P));
	memset(&S, 0, sizeof(S));
	S.day = 1;
	S.kettle_t = -1.0f;
	S.area = -1;
	S.area_card = -1;
	S.area_card_t = 100.0f;
	S.last_tread = -1;
	P.pos = v3_make(START_X, 0.0f, START_Z);
	P.pos.y = world_ground(P.pos.x, P.pos.z, 1e9f, &P.surface);
	face(START_YAW, DEG2RAD(4.0f));
	P.hover = -1;
	P.reading = -1;
	P.mode = MODE_WALK;
	radio_init();
	story_init();
	listen_init();
	director_init();
}

v3 game_eye(void)
{
	v3 stand = v3_make(P.pos.x, P.pos.y + EYE_HEIGHT + P.bob, P.pos.z);
	if (P.sit_blend > 0.0f) {
		int n;
		const interactable_t *ia = world_interactables(&n);
		float t = P.sit_blend * P.sit_blend * (3.0f - 2.0f * P.sit_blend);
		return v3_lerp(stand, ia[P.sit_id].sit_eye, t);
	}
	return stand;
}

float game_fov(void)
{
	float z = P.zoom * P.zoom * (3.0f - 2.0f * P.zoom);
	return lerpf(FOV_EYE, FOV_BINOCULARS, z);
}

bool game_radio_allowed(void)
{
	return P.mode == MODE_WALK || P.mode == MODE_SIT;
}

/* Ray through the reticle, in world space. */
v3 game_reticle_dir(void)
{
	float th = tanf(DEG2RAD(game_fov()) * 0.5f);
	float tw = th * G.aspect;
	v3 d = G.fwd;
	d = v3_add(d, v3_scale(G.right, P.reticle_x * tw));
	d = v3_add(d, v3_scale(G.up, -P.reticle_y * th));
	return v3_norm(d);
}

static bool ia_available(const interactable_t *ia)
{
	switch (ia->kind) {
	case IA_BINOCULARS: return !S.binoculars;
	case IA_NOTE: return ia->id != NOTE_POSTCARD;
	case IA_CAIRN: return !story_flag(FLAG_CAIRN_STONE);
	case IA_SHUTTERS: return S.day >= DAY_CLOSING && !story_flag(FLAG_SHUTTERS_DONE);
	case IA_CISTERN: return S.day >= DAY_CLOSING && !story_flag(FLAG_CISTERN_DONE);
	default: return true;
	}
}

static void update_hover(void)
{
	int n;
	const interactable_t *ia = world_interactables(&n);
	v3 eye = game_eye(), dir = game_reticle_dir();
	float best = 1e9f;
	P.hover = -1;
	if (P.mode != MODE_WALK && P.mode != MODE_SIT) return;
	if (P.zoom > 0.1f) return;
	for (int i = 0; i < n; i++) {
		if (P.mode == MODE_SIT && i == P.sit_id) continue;
		if (!ia_available(&ia[i])) continue;
		v3 to = v3_sub(ia[i].pos, eye);
		float dist = v3_len(to);
		if (dist > REACH + ia[i].radius) continue;
		float along = v3_dot(to, dir);
		if (along <= 0.0f) continue;
		float perp = v3_len(v3_sub(to, v3_scale(dir, along)));
		float tol = ia[i].radius + 0.06f * dist;
		if (perp < tol && perp / dist < best) {
			best = perp / dist;
			P.hover = i;
		}
	}
}

static void update_look(float dt)
{
	bool look = !radio_wants_pointer() && (P.mode == MODE_WALK || P.mode == MODE_SIT || P.mode == MODE_FINDER) &&
	            !P.input_locked;
	float sens = OPT.look_sens * (game_fov() / FOV_EYE);
	float t_yaw = 0.0f, t_pitch = 0.0f;
	float rx = 0.0f, ry = 0.0f;
	const float bx = BOX_X[OPT.comfort % 3], by = BOX_Y[OPT.comfort % 3];
	if (look && IN.ir_valid) {
		float ax = fabsf(IN.ir_x), ay = fabsf(IN.ir_y);
		t_yaw = signf(IN.ir_x) * smoothstepf(bx, 0.95f, ax) * MAX_YAW_RATE * sens;
		t_pitch = -signf(IN.ir_y) * smoothstepf(by, 0.95f, ay) * MAX_PITCH_RATE * sens;
		if (OPT.invert_pitch) t_pitch = -t_pitch;
		if (P.mode == MODE_FINDER) t_pitch = 0.0f;
		rx = clampf(IN.ir_x, -bx, bx);
		ry = clampf(IN.ir_y, -by, by);
	}
	/* IR lost (or look suspended): turning eases to a stop over ~0.25 s. */
	P.yaw_rate = approachf(P.yaw_rate, t_yaw, IN.ir_valid ? 14.0f : 12.0f, dt);
	P.pitch_rate = approachf(P.pitch_rate, t_pitch, IN.ir_valid ? 14.0f : 12.0f, dt);
	if (!P.input_locked) {
		P.reticle_x = approachf(P.reticle_x, rx, IN.ir_valid ? 30.0f : 4.0f, dt);
		P.reticle_y = approachf(P.reticle_y, ry, IN.ir_valid ? 30.0f : 4.0f, dt);
	}

	/* D-pad: snap turn with a Nunchuk, smooth turn without one. */
	if (look && P.mode != MODE_NOTEBOOK) {
		if (IN.nunchuk && OPT.snap_turn) {
			if (in_down(WPAD_BUTTON_LEFT)) P.snap_left -= DEG2RAD(30.0f);
			if (in_down(WPAD_BUTTON_RIGHT)) P.snap_left += DEG2RAD(30.0f);
		} else if (!IN.nunchuk) {
			if (in_held(WPAD_BUTTON_LEFT)) P.yaw -= DEG2RAD(60.0f) * sens * dt;
			if (in_held(WPAD_BUTTON_RIGHT)) P.yaw += DEG2RAD(60.0f) * sens * dt;
		}
	}
	float snap_step = P.snap_left * (1.0f - expf(-18.0f * dt));
	P.snap_left -= snap_step;

	P.yaw = wrap_anglef(P.yaw + P.yaw_rate * dt + snap_step);
	P.pitch = clampf(P.pitch + P.pitch_rate * dt, -PITCH_LIMIT, PITCH_LIMIT);
	if (P.mode == MODE_FINDER) P.pitch = approachf(P.pitch, DEG2RAD(-8.0f), 6.0f, dt);

	if (P.mode == MODE_SIT || (P.mode == MODE_READ && P.sit_blend > 0.5f)) {
		/* Seated: look around, but stay roughly facing out from the seat. */
		int n;
		const interactable_t *ia = world_interactables(&n);
		float rel = wrap_anglef(P.yaw - ia[P.sit_id].sit_yaw);
		P.yaw = wrap_anglef(ia[P.sit_id].sit_yaw + clampf(rel, -DEG2RAD(75.0f), DEG2RAD(75.0f)));
	}

	float target_roll = OPT.head_sway && IN.connected ? clampf(IN.roll_deg * 0.08f, -2.5f, 2.5f) : 0.0f;
	P.roll = approachf(P.roll, DEG2RAD(target_roll), 3.0f, dt);
}

static void edge_thought(int edge)
{
	switch (edge) {
	case WORLD_EDGE_GATE: think_once(TH_GATE, "That's the district line. Dot would kill me."); break;
	case WORLD_EDGE_WATER: think_once(TH_WATER, "Too cold for a swim. Too deep, anyway."); break;
	case WORLD_EDGE_RIM: think_once(TH_RIM, "Too steep. Not worth a broken ankle out here."); break;
	default: break;
	}
}

static void update_walk(float dt)
{
	float sx = IN.stick_x, sy = IN.stick_y;
	if (!IN.nunchuk) {
		sx = 0.0f;
		sy = in_held(WPAD_BUTTON_UP) ? 1.0f : (in_held(WPAD_BUTTON_DOWN) ? -1.0f : 0.0f);
	}
	if (P.mode != MODE_WALK || radio_wants_pointer() || P.zoom > 0.05f) sx = sy = 0.0f;

	/* Stride needs the Nunchuk's Z; the remote-only scheme keeps B for the radio. */
	bool stride = IN.nunchuk && in_held(WPAD_NUNCHUK_BUTTON_Z);
	float speed = (stride ? STRIDE_SPEED : WALK_SPEED) * (1.0f - 0.55f * listen_amount());   /* listening: slow down */
	v3 fwd = v3_make(sinf(P.yaw), 0.0f, -cosf(P.yaw));
	v3 right = v3_make(cosf(P.yaw), 0.0f, sinf(P.yaw));
	v3 want = v3_scale(v3_add(v3_scale(right, sx), v3_scale(fwd, sy)), speed);
	P.vel.x = approachf(P.vel.x, want.x, 9.0f, dt);
	P.vel.z = approachf(P.vel.z, want.z, 9.0f, dt);

	v3 from = P.pos;
	v3 to = v3_make(P.pos.x + P.vel.x * dt, P.pos.y, P.pos.z + P.vel.z * dt);
	int edge = 0;
	to = world_move(from, to, P.pos.y, &edge);
	if (edge && (fabsf(want.x) + fabsf(want.z)) > 0.5f) edge_thought(edge);
	float ground = world_ground(to.x, to.z, P.pos.y, &P.surface);

	/* Follow the ground: snap down steps, fall gently off anything taller. */
	if (ground >= P.pos.y - 0.65f) {
		P.pos.y = ground;
		P.vy = 0.0f;
	} else {
		P.vy -= 9.8f * dt;
		P.pos.y = fmaxf(ground, P.pos.y + P.vy * dt);
	}
	P.pos.x = to.x;
	P.pos.z = to.z;

	/* Head bob and footsteps, driven by distance travelled. */
	float moved = sqrtf((to.x - from.x) * (to.x - from.x) + (to.z - from.z) * (to.z - from.z));
	float before = P.bob_phase;
	P.bob_phase += moved / STEP_LENGTH * LL_PI;
	float sp = clampf(moved / dt / WALK_SPEED, 0.0f, 1.5f);
	P.bob = OPT.walk_bob ? sinf(P.bob_phase) * 0.018f * sp : 0.0f;
	if ((int)(before / LL_PI) != (int)(P.bob_phase / LL_PI)) {
		float pan = ((int)(P.bob_phase / LL_PI) & 1) ? 0.15f : -0.15f;
		audio_footstep(P.surface, 0.6f + 0.4f * clampf(sp, 0.0f, 1.0f), pan);
	}

	/* The singing stair. */
	int tread = world_stair_tread(P.pos);
	if (tread == SINGING_TREAD && S.last_tread != SINGING_TREAD) {
		audio_ui(UI_STAIR_NOTE);
		listen_event(SND_STAIR, v3_add(P.pos, v3_make(0, 0.1f, 0)));
		story_raise(FLAG_STAIR_HEARD);
		if (S.found[NOTE_STAIR]) think_once(TH_STAIR, "E-flat. A little flat, even. Not F, Augie.");
	}
	S.last_tread = tread;
}

static void open_text(int id)
{
	P.mode = MODE_READ;
	P.reading = id;
	audio_ui(UI_PAGE);
}

static void use(int index)
{
	int n;
	const interactable_t *ia = world_interactables(&n);
	const interactable_t *h = &ia[index];
	switch (h->kind) {
	case IA_NOTE:
		S.found[h->id] = true;
		story_note_read(h->id);
		open_text(h->id);
		if (h->id == NOTE_MAP) hint(IN.nunchuk ? "D-pad down: notebook" : "2: notebook");
		break;
	case IA_SIT:
		if (P.mode != MODE_WALK) break;
		P.mode = MODE_SIT;
		P.sit_id = index;
		P.stand_pos = P.pos;
		P.vel = v3_make(0, 0, 0);
		audio_ui(UI_TICK);
		break;
	case IA_SLEEP:
		if (S.day >= NUM_DAYS) {
			think_once(TH_LAST_NIGHT, "Last night. I don't want to sleep through it.");
		} else if (DAY.hour >= SLEEP_FROM || DAY.hour < 4.0f) {
			P.mode = MODE_SLEEP;
			S.sleep_t = 0.0f;
			audio_ui(UI_TICK);
		} else {
			game_think("Too early. Dot would laugh at me.");
		}
		break;
	case IA_LAMP:
		S.lamp_on = !S.lamp_on;
		audio_ui(UI_LAMP);
		break;
	case IA_KETTLE:
		if (S.kettle_t < 0.0f) {
			S.kettle_t = 0.0f;
			audio_ui(UI_TICK);
			if (!S.kettle_today) game_think("Hit it on the left side, she said.");
		}
		break;
	case IA_LOGBOOK:
		open_text(TEXT_LOGBOOK + S.day);
		break;
	case IA_FINDER:
		P.mode = MODE_FINDER;
		audio_ui(UI_PAGE);
		break;
	case IA_HANDSET:
		radio_call_basin();
		break;
	case IA_BINOCULARS:
		S.binoculars = true;
		audio_ui(UI_TICK);
		hint(IN.nunchuk ? "D-pad up: binoculars" : "1: binoculars");
		break;
	case IA_SUPPLY:
		if (day_is_supply(S.day)) {
			S.supply_opened[S.day] = true;
			story_raise(FLAG_SUPPLY_TODAY);
		if (S.day == DAY_POSTCARD) {
			S.found[NOTE_POSTCARD] = true;
			story_note_read(NOTE_POSTCARD);
		}
			open_text(TEXT_SUPPLY + S.day);     /* on the postcard day, A turns over to it */
		} else {
			game_think("Empty. The truck only comes when Dot says so.");
		}
		break;
	case IA_WEATHER:
		if (DAY.hour < 12.0f) S.obs_am = true;
		else S.obs_pm = true;
		story_readings_taken(DAY.hour < 12.0f);
		open_text(TEXT_WEATHER);
		break;
	case IA_CHIMES:
		audio_ui(UI_CHIMES);
		listen_event(SND_CHIMES, h->pos);
		think_once(TH_CHIMES, "They're out of tune. Of course they are.");
		break;
	case IA_GATE:
		if (story_can_leave()) {
			P.mode = MODE_ENDING;
			S.ending_t = 0.0f;
			audio_ui(UI_TICK);
		} else if (S.day >= NUM_DAYS) {
			game_think("Not yet. One more evening.");
		} else {
			game_think("Not yet. The season isn't over.");
		}
		break;
	case IA_GAUGE:
		story_raise(FLAG_GAUGE_READ);
		audio_ui(UI_TICK);
		if (S.day >= NUM_DAYS && WX.rain > 0.2f) game_think("Water in the gauge. Actual water.");
		else if (S.day == 4) game_think("Dry. A spider's moved in. Zero, then.");
		else game_think("Dry. Seven weeks of nothing.");
		break;
	case IA_CAIRN:
		story_raise(FLAG_CAIRN_STONE);
		audio_ui(UI_STONE);
		game_think("One more. It sits there like it was always going to.");
		break;
	case IA_SHUTTERS:
		story_raise(FLAG_SHUTTERS_DONE);
		audio_ui(UI_SHUTTER);
		game_think(story_flag(FLAG_CISTERN_DONE) ? "Shutters up. That's the list." : "Shutters up. Now the cistern.");
		break;
	case IA_CISTERN:
		story_raise(FLAG_CISTERN_DONE);
		audio_ui(UI_VALVE);
		game_think(story_flag(FLAG_SHUTTERS_DONE) ? "Drained. That's the list." : "Drained. Now the shutters.");
		break;
	}
}

static void update_interact(float dt)
{
	if (P.mode == MODE_READ) {
		P.read_fade = approachf(P.read_fade, 1.0f, 10.0f, dt);
		bool a = !P.input_locked && in_take(WPAD_BUTTON_A), b = !P.input_locked && in_take(WPAD_BUTTON_B);
		if (a || b) {
			/* The postcard day's supply box: the postcard comes after the contents. */
			if (P.reading == TEXT_SUPPLY + DAY_POSTCARD && a) {
				P.reading = NOTE_POSTCARD;
				audio_ui(UI_PAGE);
				return;
			}
			P.mode = S.notebook_page == 1 && P.notebook_fade > 0.5f ? MODE_NOTEBOOK
			         : (P.sit_blend > 0.5f ? MODE_SIT : MODE_WALK);
			audio_ui(UI_PAGE);
		}
		return;
	}
	P.read_fade = approachf(P.read_fade, 0.0f, 8.0f, dt);

	if (P.mode == MODE_FINDER) {
		/* A takes a bearing through the sights; B steps back. */
		if (!P.input_locked && in_take(WPAD_BUTTON_A)) {
			story_finder_sight(P.yaw);
			audio_ui(UI_TICK);
		} else if (!P.input_locked && in_take(WPAD_BUTTON_B)) {
			P.mode = MODE_WALK;
			audio_ui(UI_PAGE);
		}
		return;
	}
	if (P.input_locked || radio_in_call() || P.mode == MODE_NOTEBOOK || P.mode == MODE_SLEEP || P.mode == MODE_ENDING)
		return;
	if (!(IN.down & WPAD_BUTTON_A)) return;
	if (P.hover >= 0 || P.mode == MODE_SIT) in_take(WPAD_BUTTON_A);

	if (P.hover >= 0) {
		use(P.hover);
	} else if (P.mode == MODE_SIT) {
		P.mode = MODE_WALK;
		audio_ui(UI_TICK);
	}
}

/* D-pad up / 1: binoculars. D-pad down / 2: notebook (B or 2 closes it). */
static void update_tools(float dt)
{
	if (!P.input_locked) {
		bool other1 = in_held(WPAD_BUTTON_2), other2 = in_held(WPAD_BUTTON_1);   /* 1+2 is the dev overlay */
		if (P.mode == MODE_NOTEBOOK) {
			if ((in_take(WPAD_BUTTON_2) && !other1) | in_take(WPAD_BUTTON_B)) {
				P.mode = P.sit_blend > 0.5f ? MODE_SIT : MODE_WALK;
				audio_ui(UI_PAGE);
			}
		} else if ((P.mode == MODE_WALK || P.mode == MODE_SIT) && !radio_wants_pointer()) {
			bool bino = (IN.nunchuk && in_take(WPAD_BUTTON_UP)) | (in_take(WPAD_BUTTON_1) && !other2);
			if (P.binoculars_up && in_take(WPAD_BUTTON_MINUS)) bino = true;
			if (bino && S.binoculars) {
				P.binoculars_up = !P.binoculars_up;
				audio_ui(UI_TICK);
			}
			bool book = (IN.nunchuk && in_take(WPAD_BUTTON_DOWN)) | (in_take(WPAD_BUTTON_2) && !other1);
			if (book) {
				P.binoculars_up = false;
				P.mode = MODE_NOTEBOOK;
				audio_ui(UI_PAGE);
			}
		}
	}
	if (P.mode != MODE_WALK && P.mode != MODE_SIT) P.binoculars_up = false;
	P.zoom = approachf(P.zoom, P.binoculars_up ? 1.0f : 0.0f, 7.0f, dt);
}

static void update_places(float dt)
{
	/* Area name cards, the first time each day you arrive somewhere. */
	int a = world_area_at(P.pos.x, P.pos.z);
	if (a != S.area) {
		S.area = a;
		if (a >= 0 && !(S.areas_today & (1u << a)) && !P.input_locked && P.day_card > 7.0f) {
			S.areas_today |= 1u << a;
			S.area_card = a;
			S.area_card_t = 0.0f;
		}
	}
	S.area_card_t += dt;
	S.thought_t += dt;
	S.hint_t += dt;

	/* Playing with the Remote alone: say how, once, and again if the Nunchuk
	 * comes out later. */
	static bool s_told_remote_only;
	if (IN.connected && !IN.nunchuk && !P.input_locked && P.day_card > 6.0f) {
		if (!s_told_remote_only) hint("No Nunchuk: D-pad walks and turns, 1 binoculars, 2 notebook, hold - to listen");
		s_told_remote_only = true;
	} else if (IN.nunchuk) {
		s_told_remote_only = false;
	}

	/* The kettle: a minute on the stove, then the whistle and a cup. */
	if (S.kettle_t >= 0.0f) {
		float before = S.kettle_t;
		S.kettle_t += dt;
		if (before < 25.0f && S.kettle_t >= 25.0f) audio_ui(UI_KETTLE);
		if (before < 29.0f && S.kettle_t >= 29.0f) {
			if (!S.kettle_today) game_think("Coffee. The first good thing of the day.");
			S.kettle_today = true;
			S.kettle_t = -1.0f;
		}
	}
	S.lamp = approachf(S.lamp, S.lamp_on ? 1.0f : 0.0f, 12.0f, dt);
}

static void update_sleep(float dt)
{
	if (P.mode != MODE_SLEEP) {
		if (P.mode != MODE_ENDING) S.fade = approachf(S.fade, 0.0f, 1.6f, dt);
		return;
	}
	S.sleep_t += dt;
	S.fade = saturatef(S.sleep_t / 1.5f);
	if (S.sleep_t > 2.2f) {
		game_new_day(S.day + 1);
		S.fade = 1.0f;
		if (!P.input_locked) save_game();     /* each morning is kept */
	}
}

/* Through the gate on the last evening: fade out, a closing card, the credits. */
static void update_ending(float dt)
{
	if (P.mode != MODE_ENDING) return;
	float before = S.ending_t;
	S.ending_t += dt;
	S.fade = saturatef(S.ending_t / 3.0f);
	if (before < 2.0f && S.ending_t >= 2.0f) audio_music_cue("channel_end");   /* the motif resolves, at last */
	if (S.ending_t > ENDING_CREDITS_AT + 4.0f && !P.input_locked && in_take(WPAD_BUTTON_A)) {
		/* After the credits, A walks back up for one more look. */
		P.mode = MODE_WALK;
		S.ending_t = 0.0f;
		S.ended = true;
	}
}

void game_update(float dt)
{
	update_look(dt);
	if (!P.input_locked)                /* the screenshot tour places the camera itself */
		update_walk(dt);
	update_hover();
	update_interact(dt);
	update_tools(dt);
	if (P.mode == MODE_NOTEBOOK) notebook_update(dt);
	radio_update(dt);
	listen_update(dt);

	bool seated = P.mode == MODE_SIT || ((P.mode == MODE_READ || P.mode == MODE_NOTEBOOK) && P.sit_blend > 0.5f);
	P.sit_blend = approachf(P.sit_blend, seated ? 1.0f : 0.0f, 5.0f, dt);
	P.finder_fade = approachf(P.finder_fade, P.mode == MODE_FINDER ? 1.0f : 0.0f, 8.0f, dt);
	P.notebook_fade = approachf(P.notebook_fade, P.mode == MODE_NOTEBOOK ||
	                            (P.mode == MODE_READ && P.notebook_fade > 0.5f) ? 1.0f : 0.0f, 9.0f, dt);

	/* Let time pass: hold Z while seated (stops if the radio rings). */
	bool lapse = P.mode == MODE_SIT && P.sit_blend > 0.9f && !radio_busy() &&
	             (P.input_locked ? P.timelapse > 0.5f : in_held(WPAD_NUNCHUK_BUTTON_Z));
	DAY.rate = lapse ? DAY_TIMELAPSE_RATE : 1.0f;
	if (!P.input_locked) P.timelapse = approachf(P.timelapse, lapse ? 1.0f : 0.0f, 4.0f, dt);
	daycycle_update(dt);
	weather_update(S.day, DAY.hour, dt);
	foliage_update(dt);
	water_update(dt);
	P.day_card += dt;
	update_places(dt);
	story_update(dt);
	director_update(dt);
	update_sleep(dt);
	update_ending(dt);
}
