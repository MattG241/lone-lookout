/* Exercise the real game save reader/writer with an in-memory SD card. */
#include <stdio.h>
#include <string.h>
#include "game/game.h"
#include "game/save.h"
#include "game/guide.h"
#include "game/notebook.h"
#include "core/storage.h"

player_t P;
story_t S;
options_t OPT;
dayclock DAY;
static u8 disk[24576];
static int disk_len, checks, failures;
static bool writable = true;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

bool storage_ok(void) { return true; }
int storage_read(const char *name, void *out, u32 cap)
{
	(void)name;
	if ((u32)disk_len > cap) return -1;
	memcpy(out, disk, disk_len);
	return disk_len;
}
bool storage_write(const char *name, const void *data, u32 n)
{
	(void)name;
	if (!writable || n > sizeof(disk)) return false;
	memcpy(disk, data, n); disk_len = (int)n; return true;
}
void game_init(void) { memset(&P, 0, sizeof(P)); memset(&S, 0, sizeof(S)); guide_init(); }
void game_new_day(int day) { S.day = day; }
float world_ground(float x, float z, float ref, surface_t *surf) { (void)x; (void)z; (void)ref; (void)surf; return 10; }
void audio_set_options(float m, float w, float v, bool mono, bool hearing) { (void)m; (void)w; (void)v; (void)mono; (void)hearing; }
void story_save(sw_t *w) { sw_u8(w, 42); }
void story_load(sr_t *r) { CHECK(sr_u8(r) == 42); }
void dlg_save(sw_t *w) { sw_u8(w, 43); }
bool dlg_load(sr_t *r) { CHECK(sr_u8(r) == 43); return !r->bad; }
void radio_log_save(sw_t *w) { sw_u8(w, 44); }
void radio_log_load(sr_t *r) { CHECK(sr_u8(r) == 44); }

static void repair_header(void)
{
	sw_t w; sw_init(&w, disk + 6, 8);
	sw_u32(&w, (u32)disk_len - 14);
	sw_u32(&w, crc32_of(disk + 14, disk_len - 14));
}

int main(void)
{
	game_init();
	S.day = 4; DAY.hour = 18.25f; P.pos = v3_make(256, 20, 248);
	S.found[NOTE_MAP] = true; S.sound_day[2] = 3; S.notebook_page = NB_MOMENTS;
	OPT.manual_dialogue = true; OPT.film_grain = false;
	GUIDE.visited = 0xAB; GUIDE.selected = 3; GUIDE.pin = 3; guide_keep(0, 2);
	CHECK(save_game() && !S.save_failed);
	CHECK(save_status().has_game && save_status().day == 4);
	game_init(); OPT.manual_dialogue = false; save_init();
	CHECK(OPT.manual_dialogue && !OPT.film_grain);
	CHECK(save_continue());
	CHECK(S.day == 4 && DAY.hour == 18.25f && S.found[NOTE_MAP] && S.sound_day[2] == 3);
	CHECK(S.notebook_page == NB_MOMENTS && GUIDE.visited == 0xAB && GUIDE.pin == 3 && GUIDE.moments[0] == 2);
	/* Settings-only writes retain the stored game, including the extension. */
	game_init(); OPT.film_grain = true;
	CHECK(save_settings()); save_init(); CHECK(save_continue());
	CHECK(S.day == 4 && GUIDE.moments[0] == 2 && OPT.film_grain);
	/* The older game payload has no guide extension. Its v1 header stays valid. */
	disk_len -= 13; repair_header();
	save_init(); CHECK(save_continue());
	CHECK(S.day == 4 && S.found[NOTE_MAP] && GUIDE.pin == -1 && guide_count() == 0);
	/* Failed writes surface an error and leave the on-card bytes alone. */
	u32 before = crc32_of(disk, disk_len);
	writable = false;
	CHECK(!save_game() && S.save_failed && S.saved_t == 0);
	CHECK(before == crc32_of(disk, disk_len));
	/* A damaged body is ignored when the game boots. */
	disk[disk_len - 1] ^= 1;
	save_init(); CHECK(!save_status().has_game);
	printf("Game saves: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
