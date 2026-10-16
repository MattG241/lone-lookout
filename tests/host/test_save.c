/*
 * test_save.c -- host-side checks of the save format's building blocks:
 * the big-endian writer and reader, CRC32, and the radio script's state
 * saved by name (src/core/saveio.c, src/game/dialogue.c).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/saveio.h"
#include "game/dialogue.h"
#include "dialogue_data.h"

static int s_fails, s_checks;

#define CHECK(cond, ...) do { \
	s_checks++; \
	if (!(cond)) { s_fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* The dialogue hooks (the game provides these). */
void dlg_hook_line(int speaker, const char *text) { (void)speaker; (void)text; }
void dlg_hook_action(int action, const char *arg) { (void)action; (void)arg; }
void dlg_hook_token(const char *name, char *out, int n) { (void)name; snprintf(out, n, "1"); }
void dlg_hook_call_end(int call) { (void)call; }

static void test_bytes(void)
{
	u8 buf[256];
	sw_t w;
	sw_init(&w, buf, sizeof(buf));
	sw_u8(&w, 0xAB);
	sw_u16(&w, 0x1234);
	sw_u32(&w, 0xDEADBEEF);
	sw_f32(&w, -3.25f);
	sw_str(&w, "Kestrel, Basin.");
	sw_str(&w, "");
	CHECK(!w.overflow && w.len == 1 + 2 + 4 + 4 + 2 + 15 + 2, "writer length %u", w.len);
	CHECK(buf[1] == 0x12 && buf[2] == 0x34 && buf[3] == 0xDE, "big-endian bytes");

	sr_t r;
	sr_init(&r, buf, w.len);
	char s[32];
	CHECK(sr_u8(&r) == 0xAB, "u8");
	CHECK(sr_u16(&r) == 0x1234, "u16");
	CHECK(sr_u32(&r) == 0xDEADBEEF, "u32");
	CHECK(sr_f32(&r) == -3.25f, "f32");
	CHECK(sr_str(&r, s, sizeof(s)) == 15 && !strcmp(s, "Kestrel, Basin."), "str");
	CHECK(sr_str(&r, s, sizeof(s)) == 0 && s[0] == 0, "empty str");
	CHECK(!r.bad && r.pos == w.len, "read it all");

	/* Reading past the end is caught, never overruns. */
	CHECK(sr_u32(&r) == 0 && r.bad, "past the end");
	sr_init(&r, buf, 9);                  /* cut inside the float */
	sr_u8(&r);
	sr_u16(&r);
	sr_u32(&r);
	sr_f32(&r);
	CHECK(r.bad, "truncated float");
	/* A string longer than the buffer it's read into is cut, not overrun. */
	sr_init(&r, buf + 11, w.len - 11);
	char small[6];
	sr_str(&r, small, sizeof(small));
	CHECK(!strcmp(small, "Kestr") && !r.bad, "string truncated to %s", small);

	/* The writer stops at its capacity. */
	sw_init(&w, buf, 5);
	sw_u32(&w, 1);
	sw_u32(&w, 2);
	CHECK(w.overflow && w.len == 4, "writer overflow");
}

static void test_crc(void)
{
	CHECK(crc32_of("123456789", 9) == 0xCBF43926u, "CRC32 check value %08X", crc32_of("123456789", 9));
	u8 data[1000];
	for (int i = 0; i < 1000; i++) data[i] = (u8)(i * 37 + 11);
	u32 c = crc32_of(data, sizeof(data));
	int caught = 0;
	for (int i = 0; i < 1000; i += 7) {
		data[i] ^= 0x10;
		caught += crc32_of(data, sizeof(data)) != c;
		data[i] ^= 0x10;
	}
	CHECK(caught == (1000 + 6) / 7, "every single-bit flip changes the CRC (%d)", caught);
}

static int find(const char *id)
{
	for (int c = 0; c < DLG_NUM_CALLS; c++)
		if (!strcmp(DLG_CALLS[c].id, id)) return c;
	return -1;
}

static void test_dialogue_state(void)
{
	/* Play a few calls, set some flags, then save and load. */
	dlg_init();
	int calls[] = { find("D1_ARRIVE"), find("D1_PM"), find("D2_AM") };
	for (int k = 0; k < 3; k++) {
		CHECK(calls[k] >= 0, "call %d", k);
		dlg_force_call(calls[k]);
		for (int g = 0; g < 400 && dlg_get_state() != DS_IDLE; g++) {
			if (dlg_get_state() == DS_LINE) dlg_next();
			else if (dlg_get_state() == DS_CHOICE) dlg_choose(0);
			else dlg_close();
		}
	}
	dlg_set_flag(FLAG_NOTE_MAP, true);
	dlg_set_flag(FLAG_FAWN_NAMES, true);
	dlg_world w = { 2, 7.0f, 1, 0, -1, true };
	dlg_answer(&w);                           /* a check-in: marks it used today */
	for (int g = 0; g < 400 && dlg_get_state() != DS_IDLE; g++) {
		if (dlg_get_state() == DS_LINE) dlg_next();
		else if (dlg_get_state() == DS_CHOICE) dlg_choose(0);
		else dlg_close();
	}

	bool flags[DLG_NUM_FLAGS], done[DLG_NUM_CALLS];
	for (int f = 0; f < DLG_NUM_FLAGS; f++) flags[f] = dlg_flag(f);
	for (int c = 0; c < DLG_NUM_CALLS; c++) done[c] = dlg_call_done(c);

	static u8 buf[8192];
	sw_t sw;
	sw_init(&sw, buf, sizeof(buf));
	dlg_save(&sw);
	CHECK(!sw.overflow && sw.len < 2048, "dialogue state is %u bytes", sw.len);

	dlg_init();                               /* forget everything */
	sr_t sr;
	sr_init(&sr, buf, sw.len);
	CHECK(dlg_load(&sr), "load");
	int same = 1;
	for (int f = 0; f < DLG_NUM_FLAGS; f++) same &= flags[f] == dlg_flag(f);
	for (int c = 0; c < DLG_NUM_CALLS; c++) same &= done[c] == dlg_call_done(c);
	CHECK(same, "flags and calls survive a save");
	CHECK(dlg_call_done(calls[0]) && dlg_call_done(calls[2]) && dlg_flag(FLAG_NOTE_MAP), "the ones we set");

	/* A truncated save loads as bad, without crashing. */
	for (u32 cut = 0; cut < sw.len; cut += 5) {
		sr_init(&sr, buf, cut);
		dlg_load(&sr);
	}
	sr_init(&sr, buf, sw.len / 2);
	CHECK(!dlg_load(&sr), "half a save is rejected");

	/* Names the script no longer has are skipped; the rest still load. */
	sw_init(&sw, buf, sizeof(buf));
	sw_u16(&sw, 2);
	sw_str(&sw, "no_such_flag");
	sw_str(&sw, "note_map");
	sw_u16(&sw, 1);
	sw_str(&sw, "NO_SUCH_CALL");
	sw_u8(&sw, 1);
	sw_f32(&sw, 0.0f);
	sw_u8(&sw, 0);
	sr_init(&sr, buf, sw.len);
	CHECK(dlg_load(&sr) && dlg_flag(FLAG_NOTE_MAP), "unknown names are ignored");
}

int main(void)
{
	printf("Save tests\n");
	test_bytes();
	test_crc();
	test_dialogue_state();
	printf("%d checks, %d failed\n", s_checks, s_fails);
	return s_fails ? 1 : 0;
}
