/*
 * game.c -- walking, looking, sitting, reading, and letting time pass.
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
#include "audio/audio.h"
#include "input/input.h"
#include "gfx/gfx.h"
#include "gfx/foliage.h"

#define EYE_HEIGHT 1.62f
#define WALK_SPEED 2.2f
#define STRIDE_SPEED 3.4f
#define BOX_X 0.55f
#define BOX_Y 0.50f
#define MAX_YAW_RATE DEG2RAD(70.0f)
#define MAX_PITCH_RATE DEG2RAD(45.0f)
#define PITCH_LIMIT DEG2RAD(60.0f)
#define REACH 2.6f
#define STEP_LENGTH 0.72f

player_t P;
options_t OPT = { 1.0f, true, true, true };

static const char *NOTES[] = {
	"To whoever's next: welcome to Kestrel. Kettle sticks \xE2\x80\x94 hit it on the left. "
	"Radio's on 4. Dot's bark is worse than her bite. Her bite is also pretty bad.\n\xE2\x80\x94 A.P.",
};

const char *note_text(int id) { return (id >= 0 && id < 1) ? NOTES[id] : ""; }

void game_init(void)
{
	memset(&P, 0, sizeof(P));
	P.pos = v3_make(5.0f, 0.0f, 24.0f);
	P.pos.y = world_ground(P.pos.x, P.pos.z, 1e9f, &P.surface);
	P.yaw = DEG2RAD(-6.0f);
	P.pitch = DEG2RAD(4.0f);
	P.hover = -1;
	P.reading = -1;
	P.mode = MODE_WALK;
	radio_init();
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

float game_fov(void) { return 58.0f; }

/* Ray through the reticle, in world space. */
static v3 reticle_dir(void)
{
	float th = tanf(DEG2RAD(game_fov()) * 0.5f);
	float tw = th * G.aspect;
	v3 d = G.fwd;
	d = v3_add(d, v3_scale(G.right, P.reticle_x * tw));
	d = v3_add(d, v3_scale(G.up, -P.reticle_y * th));
	return v3_norm(d);
}

static void update_hover(void)
{
	int n;
	const interactable_t *ia = world_interactables(&n);
	v3 eye = game_eye(), dir = reticle_dir();
	float best = 1e9f;
	P.hover = -1;
	if (P.mode == MODE_READ) return;
	for (int i = 0; i < n; i++) {
		if (P.mode == MODE_SIT && i == P.sit_id) continue;
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
	bool look = !radio_wants_pointer() && P.mode != MODE_READ && !P.input_locked;
	float t_yaw = 0.0f, t_pitch = 0.0f;
	float rx = 0.0f, ry = 0.0f;
	if (look && IN.ir_valid) {
		float ax = fabsf(IN.ir_x), ay = fabsf(IN.ir_y);
		t_yaw = signf(IN.ir_x) * smoothstepf(BOX_X, 0.95f, ax) * MAX_YAW_RATE * OPT.look_sens;
		t_pitch = -signf(IN.ir_y) * smoothstepf(BOX_Y, 0.95f, ay) * MAX_PITCH_RATE * OPT.look_sens;
		rx = clampf(IN.ir_x, -BOX_X, BOX_X);
		ry = clampf(IN.ir_y, -BOX_Y, BOX_Y);
	}
	/* IR lost (or look suspended): turning eases to a stop over ~0.25 s. */
	P.yaw_rate = approachf(P.yaw_rate, t_yaw, IN.ir_valid ? 14.0f : 12.0f, dt);
	P.pitch_rate = approachf(P.pitch_rate, t_pitch, IN.ir_valid ? 14.0f : 12.0f, dt);
	if (!P.input_locked) {
		P.reticle_x = approachf(P.reticle_x, rx, IN.ir_valid ? 30.0f : 4.0f, dt);
		P.reticle_y = approachf(P.reticle_y, ry, IN.ir_valid ? 30.0f : 4.0f, dt);
	}

	/* D-pad: snap turn with a Nunchuk, smooth turn without one. */
	if (look) {
		if (IN.nunchuk && OPT.snap_turn) {
			if (in_down(WPAD_BUTTON_LEFT)) P.snap_left -= DEG2RAD(30.0f);
			if (in_down(WPAD_BUTTON_RIGHT)) P.snap_left += DEG2RAD(30.0f);
		} else if (!IN.nunchuk) {
			if (in_held(WPAD_BUTTON_LEFT)) P.yaw -= DEG2RAD(60.0f) * dt;
			if (in_held(WPAD_BUTTON_RIGHT)) P.yaw += DEG2RAD(60.0f) * dt;
		}
	}
	float snap_step = P.snap_left * (1.0f - expf(-18.0f * dt));
	P.snap_left -= snap_step;

	P.yaw = wrap_anglef(P.yaw + P.yaw_rate * dt + snap_step);
	P.pitch = clampf(P.pitch + P.pitch_rate * dt, -PITCH_LIMIT, PITCH_LIMIT);

	if (P.mode == MODE_SIT) {
		/* Seated: look around, but stay roughly facing out from the seat. */
		int n;
		const interactable_t *ia = world_interactables(&n);
		float rel = wrap_anglef(P.yaw - ia[P.sit_id].sit_yaw);
		P.yaw = wrap_anglef(ia[P.sit_id].sit_yaw + clampf(rel, -DEG2RAD(75.0f), DEG2RAD(75.0f)));
	}

	float target_roll = OPT.head_sway && IN.connected ? clampf(IN.roll_deg * 0.08f, -2.5f, 2.5f) : 0.0f;
	P.roll = approachf(P.roll, DEG2RAD(target_roll), 3.0f, dt);
}

static void update_walk(float dt)
{
	float sx = IN.stick_x, sy = IN.stick_y;
	if (!IN.nunchuk) {
		sx = 0.0f;
		sy = in_held(WPAD_BUTTON_UP) ? 1.0f : (in_held(WPAD_BUTTON_DOWN) ? -1.0f : 0.0f);
	}
	if (P.input_locked || P.mode != MODE_WALK || radio_wants_pointer()) sx = sy = 0.0f;

	/* Stride needs the Nunchuk's Z; the remote-only scheme keeps B for the radio. */
	bool stride = IN.nunchuk && in_held(WPAD_NUNCHUK_BUTTON_Z);
	float speed = stride ? STRIDE_SPEED : WALK_SPEED;
	v3 fwd = v3_make(sinf(P.yaw), 0.0f, -cosf(P.yaw));
	v3 right = v3_make(cosf(P.yaw), 0.0f, sinf(P.yaw));
	v3 want = v3_scale(v3_add(v3_scale(right, sx), v3_scale(fwd, sy)), speed);
	P.vel.x = approachf(P.vel.x, want.x, 9.0f, dt);
	P.vel.z = approachf(P.vel.z, want.z, 9.0f, dt);

	v3 from = P.pos;
	v3 to = v3_make(P.pos.x + P.vel.x * dt, P.pos.y, P.pos.z + P.vel.z * dt);
	to = world_move(from, to, P.pos.y);
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
}

static void update_interact(float dt)
{
	int n;
	const interactable_t *ia = world_interactables(&n);

	if (P.mode == MODE_READ) {
		P.read_fade = approachf(P.read_fade, 1.0f, 10.0f, dt);
		if (!P.input_locked && (in_down(WPAD_BUTTON_A) || in_down(WPAD_BUTTON_B))) {
			P.mode = P.sit_blend > 0.5f ? MODE_SIT : MODE_WALK;
			audio_ui(UI_PAGE);
		}
		return;
	}
	P.read_fade = approachf(P.read_fade, 0.0f, 8.0f, dt);

	if (P.input_locked || radio_in_call()) return;
	if (!in_down(WPAD_BUTTON_A)) return;

	if (P.hover >= 0) {
		const interactable_t *h = &ia[P.hover];
		if (h->kind == IA_NOTE) {
			P.mode = MODE_READ;
			P.reading = h->id;
			audio_ui(UI_PAGE);
		} else if (h->kind == IA_SIT && P.mode == MODE_WALK) {
			P.mode = MODE_SIT;
			P.sit_id = P.hover;
			P.stand_pos = P.pos;
			P.vel = v3_make(0, 0, 0);
			audio_ui(UI_TICK);
		}
	} else if (P.mode == MODE_SIT) {
		P.mode = MODE_WALK;
		audio_ui(UI_TICK);
	}
}

void game_update(float dt)
{
	update_look(dt);
	if (!P.input_locked)                /* the screenshot tour places the camera itself */
		update_walk(dt);
	update_hover();
	update_interact(dt);
	radio_update(dt);

	P.sit_blend = approachf(P.sit_blend, P.mode == MODE_SIT || (P.mode == MODE_READ && P.sit_blend > 0.5f) ? 1.0f : 0.0f,
	                        5.0f, dt);

	/* Let time pass: hold Z while seated (stops if the radio rings). */
	bool lapse = P.mode == MODE_SIT && P.sit_blend > 0.9f && !radio_busy() &&
	             (P.input_locked ? P.timelapse > 0.5f : in_held(WPAD_NUNCHUK_BUTTON_Z));
	DAY.rate = lapse ? DAY_TIMELAPSE_RATE : 1.0f;
	if (!P.input_locked) P.timelapse = approachf(P.timelapse, lapse ? 1.0f : 0.0f, 4.0f, dt);
	daycycle_update(dt);
	foliage_update(dt);
	P.day_card += dt;

	/* The Day 1 radio check comes in a little after you arrive. */
	static float s_call_timer = 0.0f;
	static bool s_called = false;
	s_call_timer += dt;
	if (!s_called && !P.input_locked && s_call_timer > 20.0f && !radio_busy()) {
		radio_ring(CALL_D1_RADIO_CHECK);
		s_called = true;
	}
}
