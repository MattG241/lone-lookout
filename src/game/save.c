/*
 * save.c -- see save.h.
 *
 * Layout (all big-endian):
 *   u32 magic 'LLSV', u16 version, u32 body length, u32 CRC32 of the body
 *   body: settings section; u8 has_game; game section
 * The game section is kept as bytes after reading or writing it, so saving
 * the settings alone never loses a season.
 */
#include <string.h>
#include "game/save.h"
#include "game/game.h"
#include "game/story.h"
#include "game/radio.h"
#include "game/listen.h"
#include "game/director.h"
#include "audio/audio.h"
#include "core/saveio.h"
#include "core/storage.h"

#define SAVE_MAGIC 0x4C4C5356u        /* "LLSV" */
#define SAVE_VERSION 1
#define HEADER_BYTES 14
#define MAX_BYTES (24 * 1024)

static u8 s_file[MAX_BYTES];
static u8 s_game[MAX_BYTES];          /* the last known game section */
static u32 s_game_len;
static bool s_has_game;
static int s_day;
static float s_hour;

void options_apply(void)
{
	audio_set_options(OPT.vol_music / 8.0f, OPT.vol_ambience / 8.0f, OPT.vol_voice / 8.0f, OPT.mono, OPT.hearing);
}

/* ------------------------------------------------------------------------ */

static void write_settings(sw_t *w)
{
	sw_f32(w, OPT.look_sens);
	sw_u8(w, OPT.comfort);
	u16 bits = (u16)(OPT.invert_pitch | OPT.snap_turn << 1 | OPT.head_sway << 2 | OPT.walk_bob << 3 |
	                 OPT.compass << 4 | OPT.subs_large << 5 | OPT.subs_backing << 6 | OPT.hearing << 7 |
	                 OPT.mono << 8 | OPT.lock30 << 9);
	sw_u16(w, bits);
	sw_u8(w, OPT.vol_music);
	sw_u8(w, OPT.vol_ambience);
	sw_u8(w, OPT.vol_voice);
}

static void read_settings(sr_t *r)
{
	options_t o = OPT;
	o.look_sens = clampf(sr_f32(r), 0.5f, 2.0f);
	o.comfort = sr_u8(r) % 3;
	u16 b = sr_u16(r);
	o.invert_pitch = b & 1;
	o.snap_turn = (b >> 1) & 1;
	o.head_sway = (b >> 2) & 1;
	o.walk_bob = (b >> 3) & 1;
	o.compass = (b >> 4) & 1;
	o.subs_large = (b >> 5) & 1;
	o.subs_backing = (b >> 6) & 1;
	o.hearing = (b >> 7) & 1;
	o.mono = (b >> 8) & 1;
	o.lock30 = (b >> 9) & 1;
	o.vol_music = sr_u8(r) % 11;
	o.vol_ambience = sr_u8(r) % 11;
	o.vol_voice = sr_u8(r) % 11;
	if (!r->bad) OPT = o;
}

static u16 bits_of(const bool *b, int n)
{
	u16 v = 0;
	for (int i = 0; i < n && i < 16; i++) v |= (u16)(b[i] ? 1u << i : 0u);
	return v;
}

static void write_game(sw_t *w)
{
	sw_u8(w, (u8)S.day);
	sw_f32(w, DAY.hour);
	sw_f32(w, P.pos.x);
	sw_f32(w, P.pos.y);
	sw_f32(w, P.pos.z);
	sw_f32(w, P.yaw);
	sw_f32(w, P.pitch);
	sw_u16(w, bits_of(S.found, NOTE_COUNT));
	sw_u16(w, bits_of(S.supply_opened, NUM_DAYS + 1));
	sw_bytes(w, S.sound_day, SND_COUNT);
	sw_u32(w, S.thoughts_said);
	u8 flags = (u8)(S.binoculars | S.obs_am << 1 | S.obs_pm << 2 | S.kettle_today << 3 | S.lamp_on << 4 | S.ended << 5);
	sw_u8(w, flags);
	sw_u32(w, S.areas_today);
	sw_u8(w, (u8)S.notebook_page);
	sw_u8(w, (u8)S.notebook_sel);
	sw_u8(w, (u8)S.sound_sel);
	story_save(w);
	dlg_save(w);
	radio_log_save(w);
}

/* Read only the day and hour, for the title screen. */
static void peek_game(const u8 *p, u32 n)
{
	sr_t r;
	sr_init(&r, p, n);
	s_day = sr_u8(&r);
	s_hour = sr_f32(&r);
}

static bool read_game(const u8 *p, u32 n)
{
	sr_t r;
	sr_init(&r, p, n);
	int day = sr_u8(&r);
	if (day < 1 || day > NUM_DAYS) return false;
	game_init();
	game_new_day(day);
	DAY.hour = clampf(sr_f32(&r), 0.0f, 29.0f);
	P.pos.x = sr_f32(&r);
	P.pos.y = sr_f32(&r);
	P.pos.z = sr_f32(&r);
	P.yaw = sr_f32(&r);
	P.pitch = clampf(sr_f32(&r), -1.0f, 1.0f);
	u16 found = sr_u16(&r), supply = sr_u16(&r);
	for (int i = 0; i < NOTE_COUNT; i++) S.found[i] = (found >> i) & 1;
	for (int i = 0; i <= NUM_DAYS; i++) S.supply_opened[i] = (supply >> i) & 1;
	sr_bytes(&r, S.sound_day, SND_COUNT);
	S.thoughts_said = sr_u32(&r);
	u8 f = sr_u8(&r);
	S.binoculars = f & 1;
	S.obs_am = (f >> 1) & 1;
	S.obs_pm = (f >> 2) & 1;
	S.kettle_today = (f >> 3) & 1;
	S.lamp_on = (f >> 4) & 1;
	S.ended = (f >> 5) & 1;
	S.areas_today = sr_u32(&r);
	S.notebook_page = sr_u8(&r) % 5;
	S.notebook_sel = sr_u8(&r) % NOTE_COUNT;
	S.sound_sel = sr_u8(&r) % SND_COUNT;
	story_load(&r);
	dlg_load(&r);
	radio_log_load(&r);
	if (r.bad) return false;
	/* A position that isn't on the map (or not on solid ground) falls back to
	 * the morning spot beside the cot. */
	if (!(P.pos.x > 2.0f && P.pos.x < WORLD_SIZE - 2.0f && P.pos.z > 2.0f && P.pos.z < WORLD_SIZE - 2.0f)) {
		float h = DAY.hour;
		game_new_day(day);
		DAY.hour = h;
	} else {
		P.pos.y = world_ground(P.pos.x, P.pos.z, P.pos.y + 0.5f, &P.surface);
	}
	P.day_card = 0.0f;                  /* show the day's card again */
	S.season_dirty = true;
	return true;
}

/* ------------------------------------------------------------------------ */

static bool write_file(bool with_game)
{
	static u8 body[MAX_BYTES];
	sw_t w;
	sw_init(&w, body, sizeof(body));
	write_settings(&w);
	sw_u8(&w, with_game ? 1 : (s_has_game ? 1 : 0));
	if (with_game) {
		sw_t g;
		sw_init(&g, s_game, sizeof(s_game));
		write_game(&g);
		if (g.overflow) return false;
		s_game_len = g.len;
		s_has_game = true;
		peek_game(s_game, s_game_len);
	}
	if (s_has_game) sw_bytes(&w, s_game, s_game_len);
	if (w.overflow) return false;

	sw_t h;
	sw_init(&h, s_file, sizeof(s_file));
	sw_u32(&h, SAVE_MAGIC);
	sw_u16(&h, SAVE_VERSION);
	sw_u32(&h, w.len);
	sw_u32(&h, crc32_of(body, w.len));
	sw_bytes(&h, body, w.len);
	if (h.overflow) return false;
	return storage_write(SAVE_NAME, s_file, h.len);
}

void save_init(void)
{
	s_has_game = false;
	int n = storage_read(SAVE_NAME, s_file, sizeof(s_file));
	if (n >= HEADER_BYTES) {
		sr_t r;
		sr_init(&r, s_file, (u32)n);
		u32 magic = sr_u32(&r);
		u16 version = sr_u16(&r);
		u32 len = sr_u32(&r), crc = sr_u32(&r);
		const u8 *body = s_file + HEADER_BYTES;
		/* A wrong version, a short file or a bad CRC: start fresh (the file is
		 * left alone until the next save replaces it). */
		if (magic == SAVE_MAGIC && version == SAVE_VERSION && len <= (u32)n - HEADER_BYTES &&
		    crc32_of(body, len) == crc) {
			sr_t b;
			sr_init(&b, body, len);
			read_settings(&b);
			s_has_game = sr_u8(&b) != 0;
			if (s_has_game && !b.bad && b.pos < len) {
				s_game_len = len - b.pos;
				memcpy(s_game, body + b.pos, s_game_len);
				peek_game(s_game, s_game_len);
			} else {
				s_has_game = false;
			}
		}
	}
	options_apply();
}

save_summary save_status(void)
{
	save_summary s = { storage_ok(), s_has_game, s_day, s_hour };
	return s;
}

bool save_game(void) { return write_file(true); }
bool save_settings(void) { return write_file(false); }

bool save_continue(void)
{
	if (!s_has_game) return false;
	bool ok = read_game(s_game, s_game_len);
	options_apply();
	return ok;
}

void save_forget_game(void)
{
	s_has_game = false;
	s_game_len = 0;
}
