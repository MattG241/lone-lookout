/*
 * test_story.c -- host-side checks of the radio script, and whole-season
 * playthroughs of the dialogue runtime.
 *
 * Built and run by `make test` next to test_world.c. It links the game's own
 * dialogue.c against the compiled script in gen/, and supplies the dlg_hook_*
 * functions the game would (game/story.c), so what runs here is what runs on
 * the Wii.
 *
 * 1. Script structure: every op of every call is reachable, every path
 *    ends, every jump stays inside its call, every token is known.
 * 2. Playthroughs, at one-second steps of real time with the game's clock
 *    (120 s per game hour, time-lapse x40):
 *      - the story path: a player who does each day's duties and errand,
 *        answers every call and sleeps when the day winds down. Every story
 *        call must play, the season must end, and the real time it took is
 *        the pacing estimate (DESIGN.md section 3: 4-6 hours).
 *      - ignore everything: never answers, never reads, sits and lets time
 *        pass, sleeps at seven. The season must still end at the gate.
 *      - many randomised players: random answer delays, missed calls,
 *        random choices, skipped errands. Nothing may stick, every line
 *        must be clean, and every season must end.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/dialogue.h"
#include "dialogue_data.h"
#include "audio/voice.h"

static int s_fails, s_checks;

#define CHECK(cond, ...) do { \
	s_checks++; \
	if (!(cond)) { s_fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* The game's numbers (game/daycycle.h, game/radio.c, game/story.c). */
#define SECONDS_PER_HOUR 120.0f
#define TIMELAPSE 40.0f
#define RING_GIVE_UP 45.0f
#define LINE_GAP 0.8f
#define ELK_SECONDS 82.0f
#define NUM_DAYS 11
#define GATE_HOUR 20.5f

/* ------------------------------------------------------------------------ */
/* 1. Script structure                                                      */
/* ------------------------------------------------------------------------ */

static int call_end(int c)
{
	/* Calls are laid out one after another; a call ends where the next begins. */
	int end = (int)(sizeof(DLG_OPS) / sizeof(DLG_OPS[0]));
	for (int i = 0; i < DLG_NUM_CALLS; i++)
		if (DLG_CALLS[i].first > DLG_CALLS[c].first && DLG_CALLS[i].first < end) end = DLG_CALLS[i].first;
	return end;
}

static bool known_tokens(const char *s)
{
	static const char *OK[] = { "dry", "wet", "rh", "wind", "fuel", "bearing" };
	for (const char *p = strchr(s, '{'); p; p = strchr(p + 1, '{')) {
		const char *e = strchr(p, '}');
		if (!e) return false;
		bool found = false;
		for (int k = 0; k < 6; k++)
			if ((int)strlen(OK[k]) == e - p - 1 && !strncmp(p + 1, OK[k], e - p - 1)) found = true;
		if (!found) return false;
	}
	return true;
}

static void test_script(void)
{
	int lines = 0, choices = 0;
	for (int c = 0; c < DLG_NUM_CALLS; c++) {
		const dlg_call *call = &DLG_CALLS[c];
		int first = call->first, end = call_end(c);
		CHECK(end > first, "%s has no ops", call->id);
		CHECK(call->day_min >= 1 && call->day_max <= NUM_DAYS && call->day_min <= call->day_max, "%s: days %d-%d",
		      call->id, call->day_min, call->day_max);
		CHECK(call->t_from < call->t_to && call->t_to <= call->t_retry, "%s: times %d %d %d", call->id, call->t_from,
		      call->t_to, call->t_retry);
		/* Walk every path from the first op. */
		static unsigned char seen[4096];
		int stack[512], sp = 0;
		memset(seen, 0, sizeof(seen));
		stack[sp++] = first;
		bool ends = false;
		while (sp > 0) {
			int pc = stack[--sp];
			if (pc < first || pc >= end) {
				CHECK(0, "%s: jump to op %d outside the call (%d..%d)", call->id, pc, first, end - 1);
				continue;
			}
			if (seen[pc - first]) continue;
			seen[pc - first] = 1;
			const dlg_op *o = &DLG_OPS[pc];
			switch (o->op) {
			case DLG_LINE:
				CHECK(o->a <= SPK_CREW, "%s: speaker %d", call->id, o->a);
				CHECK(known_tokens(DLG_STRINGS[o->str]), "%s: unknown token in \"%s\"", call->id, DLG_STRINGS[o->str]);
				CHECK(strlen(DLG_STRINGS[o->str]) < 360, "%s: line too long", call->id);
				stack[sp++] = pc + 1;
				break;
			case DLG_CHOICE:
				CHECK(o->a >= 1 && o->a <= 4, "%s: choice with %d options", call->id, o->a);
				for (int k = 0; k < o->a; k++) {
					const dlg_op *opt = &DLG_OPS[pc + 1 + k];
					CHECK(opt->op == DLG_OPTION, "%s: choice op %d not followed by options", call->id, pc);
					CHECK(known_tokens(DLG_STRINGS[opt->str]), "%s: unknown token in option", call->id);
					CHECK(strlen(DLG_STRINGS[opt->str]) < 120, "%s: option too long: %s", call->id, DLG_STRINGS[opt->str]);
					seen[pc + 1 + k - first] = 1;
					stack[sp++] = opt->target;
				}
				stack[sp++] = pc + 1 + o->a;      /* no option visible: skip past them */
				break;
			case DLG_GOTO: stack[sp++] = o->target; break;
			case DLG_BRANCH:
				stack[sp++] = o->target;
				stack[sp++] = pc + 1;
				break;
			case DLG_SET:
			case DLG_CLEAR:
				CHECK(o->set >= 0 && o->set < DLG_NUM_FLAGS, "%s: flag %d", call->id, o->set);
				stack[sp++] = pc + 1;
				break;
			case DLG_DO: stack[sp++] = pc + 1; break;
			case DLG_END: ends = true; break;
			default: CHECK(0, "%s: stray op %d at %d", call->id, o->op, pc); break;
			}
			if (sp > 500) break;
		}
		CHECK(ends, "%s never ends", call->id);
		for (int pc = first; pc < end; pc++) {
			CHECK(seen[pc - first], "%s: op %d (%d) is unreachable", call->id, pc, DLG_OPS[pc].op);
			if (DLG_OPS[pc].op == DLG_LINE) lines++;
			if (DLG_OPS[pc].op == DLG_CHOICE) choices++;
		}
	}
	/* The fallback check-in must always be there to answer. */
	int last = -1;
	for (int c = 0; c < DLG_NUM_CALLS; c++)
		if (DLG_CALLS[c].kind == DLG_KIND_CHECKIN) last = c;
	CHECK(last >= 0 && DLG_CALLS[last].day_min == 1 && DLG_CALLS[last].day_max == NUM_DAYS &&
	      DLG_CALLS[last].where_kind == DLG_WHERE_ANY && DLG_CALLS[last].needs[0] == 0,
	      "the last check-in must be an any-day, anywhere fallback");
	printf("  script: %d calls, %d lines, %d choices\n", DLG_NUM_CALLS, lines, choices);
}

/* ------------------------------------------------------------------------ */
/* 2. Playthroughs                                                          */
/* ------------------------------------------------------------------------ */

/* Where the simulated player can be (world/sites.h, world_data.h). */
enum {
	P_TRAILHEAD, P_CAB, P_DECK, P_STOOL, P_CATWALK, P_SPRING, P_MEADOW, P_LOG, P_GAUGE, P_TARN, P_DOCK, P_RIDGE,
	P_BENCH, P_CAIRN, P_BURN, P_FALLEN, P_CABIN, P_PORCH, P_COUNT
};
static const struct { float x, z; int area, indoor, seat; } PLACE[P_COUNT] = {
	[P_TRAILHEAD] = { 250, 497, -1, 0, -1 }, [P_CAB] = { 256, 248, 0, 1, -1 },  [P_DECK] = { 256, 248, 0, 0, -1 },
	[P_STOOL] = { 256, 248, 0, 1, 0 },        [P_CATWALK] = { 254, 247, 0, 0, 1 }, [P_SPRING] = { 196, 213, -1, 0, -1 },
	[P_MEADOW] = { 120, 330, 1, 0, -1 },      [P_LOG] = { 101, 341, 1, 0, 2 },  [P_GAUGE] = { 113, 334, 1, 0, -1 },
	[P_TARN] = { 124, 140, 2, 0, -1 },        [P_DOCK] = { 132, 150, 2, 0, 3 }, [P_RIDGE] = { 398, 112, 3, 0, -1 },
	[P_BENCH] = { 403, 119, 3, 0, 4 },        [P_CAIRN] = { 402, 106, 3, 0, -1 }, [P_BURN] = { 392, 372, 4, 0, -1 },
	[P_FALLEN] = { 401, 381, 4, 0, 5 },       [P_CABIN] = { 244, 434, 5, 2, -1 }, [P_PORCH] = { 243, 438, 5, 0, 6 },
};

enum { DO_WAIT, DO_LAPSE, DO_RAISE, DO_READ_AM, DO_READ_PM, DO_CALL, DO_SLEEP, DO_GATE };
typedef struct {
	float hour;              /* set off no earlier than this */
	int place;
	int act;
	float until;             /* DO_WAIT / DO_LAPSE: stay until this hour */
	int flag;                /* DO_RAISE */
} step_t;

#define W(h, p, u) { h, p, DO_WAIT, u, -1 }
#define L(h, p, u) { h, p, DO_LAPSE, u, -1 }
#define R(h, p, f) { h, p, DO_RAISE, 0, f }
#define AM(h) { h, P_DECK, DO_READ_AM, 0, -1 }
#define PM(h) { h, P_DECK, DO_READ_PM, 0, -1 }
#define CALL(h, p) { h, p, DO_CALL, 0, -1 }
#define SLEEP(h) { h, P_CAB, DO_SLEEP, 0, -1 }
#define END { -1, 0, 0, 0, -1 }
/* Waking in the cab: the kettle, the morning readings, Dot's call. */
#define MORNING W(6.0f, P_CAB, 6.3f), AM(6.3f), W(6.4f, P_CAB, 6.8f)

/* The story path, day by day: duties, the errand, some wandering and sitting
 * (in real time), and one evening's wait time-lapsed on a seat. */
static const step_t STORY[NUM_DAYS + 1][24] = {
	[1] = { W(15.5f, P_TRAILHEAD, 15.6f), R(15.6f, P_CAB, FLAG_NOTE_WELCOME), R(16.8f, P_CAB, FLAG_NOTE_MAP),
	        R(16.8f, P_CAB, FLAG_NOTE_BIRDS), W(17.0f, P_DECK, 17.6f), R(17.6f, P_MEADOW, FLAG_NOTE_FENCE),
	        W(18.6f, P_LOG, 19.3f), W(19.3f, P_CAB, 21.2f), SLEEP(21.3f), END },
	[2] = { MORNING, R(6.8f, P_MEADOW, FLAG_FAWNS_SEEN), W(7.9f, P_MEADOW, 8.4f), R(8.4f, P_SPRING, FLAG_NOTE_SPRING),
	        R(9.2f, P_TARN, FLAG_NOTE_DOCK), W(10.0f, P_DOCK, 11.4f), W(11.4f, P_CABIN, 12.4f), W(12.4f, P_PORCH, 13.6f),
	        R(13.6f, P_DECK, FLAG_STAIR_HEARD), W(14.6f, P_CATWALK, 16.8f), PM(17.0f), W(17.1f, P_CAB, 18.6f),
	        L(18.6f, P_CATWALK, 20.4f), W(20.4f, P_CAB, 21.2f), SLEEP(21.2f), END },
	[3] = { MORNING, W(6.8f, P_TARN, 8.6f), W(8.6f, P_DOCK, 9.6f), R(9.6f, P_TRAILHEAD, FLAG_SUPPLY_TODAY),
	        W(12.0f, P_CABIN, 13.0f), W(13.0f, P_PORCH, 14.6f), W(14.6f, P_MEADOW, 16.2f), PM(17.0f),
	        W(17.2f, P_CAB, 18.2f), L(18.2f, P_CATWALK, 20.2f), W(20.2f, P_CAB, 21.2f), SLEEP(21.2f), END },
	[4] = { MORNING, R(6.8f, P_GAUGE, FLAG_GAUGE_READ), R(8.3f, P_LOG, FLAG_SOUND_LOGGED), W(8.4f, P_LOG, 8.8f),
	        R(8.8f, P_SPRING, FLAG_AT_SPRING), R(10.2f, P_CAIRN, FLAG_CAIRN_STONE), R(11.8f, P_BENCH, FLAG_NOTE_BENCH),
	        W(12.0f, P_BENCH, 13.0f), R(13.0f, P_BURN, FLAG_NOTE_SNAG), W(14.4f, P_FALLEN, 15.4f), PM(16.8f),
	        W(17.0f, P_CAB, 18.4f), L(18.4f, P_CATWALK, 20.2f), W(20.2f, P_CAB, 21.2f), SLEEP(21.2f), END },
	[5] = { MORNING, R(6.8f, P_CABIN, FLAG_NOTE_FLOWERS), R(8.0f, P_CABIN, FLAG_NOTE_LETTER), W(8.2f, P_PORCH, 9.6f),
	        W(9.6f, P_TARN, 11.4f), W(11.4f, P_DECK, 13.8f), W(13.8f, P_CAB, 16.6f), R(16.6f, P_CAB, FLAG_SMOKE_SEEN),
	        R(16.7f, P_CAB, FLAG_SMOKE_SIGHTED), CALL(16.75f, P_CAB), PM(17.2f), W(17.3f, P_CAB, 18.8f),
	        L(18.8f, P_STOOL, 20.8f), W(20.8f, P_CAB, 21.3f), SLEEP(21.3f), END },
	[6] = { MORNING, W(6.8f, P_RIDGE, 10.4f), W(10.4f, P_BENCH, 11.6f), W(11.6f, P_BURN, 13.4f),
	        W(13.4f, P_FALLEN, 14.6f), W(14.6f, P_CABIN, 16.0f), PM(17.0f), W(17.1f, P_CAB, 18.6f),
	        L(18.6f, P_CATWALK, 20.6f), W(20.6f, P_CAB, 21.4f), SLEEP(21.4f), END },
	[7] = { MORNING, W(6.8f, P_TARN, 8.8f), W(8.8f, P_DOCK, 10.2f), W(10.2f, P_MEADOW, 12.4f),
	        W(12.4f, P_CABIN, 14.0f), W(14.0f, P_BENCH, 16.8f), L(16.8f, P_BENCH, 18.95f), W(18.95f, P_BENCH, 20.3f),
	        W(20.3f, P_CAB, 21.8f), SLEEP(21.8f), END },
	[8] = { MORNING, W(6.8f, P_BURN, 8.8f), W(8.8f, P_CABIN, 9.8f), W(9.8f, P_PORCH, 11.2f), W(11.2f, P_TARN, 13.0f),
	        L(13.0f, P_DOCK, 15.0f), PM(16.6f), W(17.0f, P_CAB, 17.4f), W(17.4f, P_LOG, 20.4f), W(20.4f, P_CAB, 21.4f),
	        SLEEP(21.4f), END },
	[9] = { MORNING, R(6.8f, P_TRAILHEAD, FLAG_SUPPLY_TODAY), R(8.0f, P_TRAILHEAD, FLAG_NOTE_POSTCARD),
	        W(8.2f, P_CABIN, 9.2f), W(9.2f, P_PORCH, 10.6f), W(10.6f, P_MEADOW, 12.6f), W(12.6f, P_LOG, 14.0f),
	        W(14.0f, P_SPRING, 15.0f), PM(16.6f), W(17.0f, P_CAB, 18.6f), L(18.6f, P_CATWALK, 20.8f),
	        W(20.8f, P_CAB, 21.8f), SLEEP(21.8f), END },
	[10] = { MORNING, W(6.8f, P_CABIN, 8.4f), R(8.4f, P_CABIN, FLAG_SHUTTERS_DONE), R(8.6f, P_DECK, FLAG_CISTERN_DONE),
	         W(9.8f, P_RIDGE, 11.6f), W(11.6f, P_BENCH, 13.0f), W(13.0f, P_CAIRN, 13.4f), W(13.4f, P_BURN, 15.4f),
	         PM(17.0f), W(17.1f, P_CAB, 18.4f), L(18.4f, P_CATWALK, 20.4f), W(20.4f, P_CAB, 21.3f), SLEEP(21.3f), END },
	[11] = { MORNING, W(6.8f, P_MEADOW, 9.0f), W(9.0f, P_CABIN, 12.4f), W(12.4f, P_PORCH, 14.0f), W(14.0f, P_DECK, 16.0f),
	         L(16.0f, P_CATWALK, 19.4f), W(19.4f, P_CAB, 20.0f), { 20.0f, P_TRAILHEAD, DO_GATE, 0, -1 }, END },
};

/* Ignore everything: sit on the stool, let the day pass, sleep at seven. */
static const step_t IGNORE[NUM_DAYS + 1][4] = {
	[1] = { L(15.5f, P_STOOL, 19.0f), SLEEP(19.0f), END },
	[2] = { L(6.0f, P_STOOL, 19.0f), SLEEP(19.0f), END }, [3] = { L(6.0f, P_STOOL, 19.0f), SLEEP(19.0f), END },
	[4] = { L(6.0f, P_STOOL, 19.0f), SLEEP(19.0f), END }, [5] = { L(6.0f, P_STOOL, 19.0f), SLEEP(19.0f), END },
	[6] = { L(6.0f, P_STOOL, 19.0f), SLEEP(19.0f), END }, [7] = { L(6.0f, P_STOOL, 19.0f), SLEEP(19.0f), END },
	[8] = { L(6.0f, P_STOOL, 19.0f), SLEEP(19.0f), END }, [9] = { L(6.0f, P_STOOL, 19.0f), SLEEP(19.0f), END },
	[10] = { L(6.0f, P_STOOL, 19.0f), SLEEP(19.0f), END },
	[11] = { L(6.0f, P_STOOL, 19.6f), { 19.6f, P_TRAILHEAD, DO_GATE, 0, -1 }, END },
};

typedef struct {
	const char *name;
	const step_t *plan;          /* NUM_DAYS + 1 rows of 'stride' steps */
	int stride;
	bool answers;
	float answer_delay;          /* seconds after the ring starts */
	bool random;                 /* random choices, delays, misses and skipped steps */
	unsigned seed;
} player_t;

static struct {
	int day;
	float hour, real;            /* game hour; real seconds since the season began */
	float day_real[NUM_DAYS + 1];
	int place;                   /* where the player is (-1 while walking) */
	bool season_over, ended;
	float elk_t;                 /* < 0: no bulls; else seconds of listening */
	bool want_call;              /* something to report to Basin */
	const char *errand;
	int lines, words, calls_played;
	unsigned rng;
	char last_line[400];
	bool bad_line;
} SIM;

static unsigned rnd(void)
{
	SIM.rng = SIM.rng * 1103515245u + 12345u;
	return (SIM.rng >> 8) & 0xFFFF;
}
static float frand(void) { return rnd() / 65535.0f; }

/* --- The hooks the game would provide --- */

void dlg_hook_line(int speaker, const char *text)
{
	SIM.lines++;
	for (const char *p = text; *p; p++)
		if (*p == ' ') SIM.words++;
	snprintf(SIM.last_line, sizeof(SIM.last_line), "%s", text);
	if (strchr(text, '{') || strchr(text, '}') || !text[0]) SIM.bad_line = true;
	(void)speaker;
}

void dlg_hook_action(int action, const char *arg)
{
	switch (action) {
	case ACT_ERRAND: SIM.errand = arg; break;
	case ACT_ELK: SIM.elk_t = 0.0f; break;
	case ACT_SEASON_OVER: SIM.season_over = true; break;
	default: break;
	}
}

void dlg_hook_token(const char *name, char *out, int n)
{
	if (!strcmp(name, "wind")) snprintf(out, n, "W at 6");
	else if (!strcmp(name, "fuel")) snprintf(out, n, "7.5");
	else if (!strcmp(name, "bearing")) snprintf(out, n, "104");
	else snprintf(out, n, "58");
}

void dlg_hook_call_end(int call)
{
	(void)call;
	SIM.calls_played++;
}

/* --- The simulation --- */

static dlg_world world(bool can_ring)
{
	dlg_world w;
	w.day = SIM.day;
	w.hour = SIM.hour >= 24.0f ? SIM.hour - 24.0f : SIM.hour;
	int p = SIM.place;
	w.area = p >= 0 ? PLACE[p].area : -1;
	w.indoor = p >= 0 ? PLACE[p].indoor : 0;
	w.seat = p >= 0 ? PLACE[p].seat : -1;
	w.can_ring = can_ring;
	return w;
}

static bool can_leave(void) { return SIM.day >= NUM_DAYS && (SIM.season_over || SIM.hour >= GATE_HOUR); }

/* The game's story_new_day. */
static void new_day(int day)
{
	SIM.day = day;
	SIM.hour = day == 1 ? 15.5f : 6.0f;
	SIM.place = day == 1 ? P_TRAILHEAD : P_CAB;
	SIM.errand = NULL;
	dlg_set_flag(FLAG_OBS_AM, false);
	dlg_set_flag(FLAG_OBS_PM, false);
	dlg_set_flag(FLAG_AT_SPRING, false);
	dlg_set_flag(FLAG_SUPPLY_TODAY, false);
}

static float walk_seconds(int from, int to)
{
	if (from < 0 || from == to) return 0.0f;
	float dx = PLACE[to].x - PLACE[from].x, dz = PLACE[to].z - PLACE[from].z;
	float s = sqrtf(dx * dx + dz * dz) * 1.35f / 2.4f;         /* trails wind; a walk with some striding */
	if ((PLACE[from].area == 0) != (PLACE[to].area == 0)) s += 25.0f;   /* the tower stairs */
	return s;
}

/* One second of real time. 'lapse' asks for time-lapse (seated); the radio stops it. */
static float s_ring_t, s_wait, s_wake;

static void second(const player_t *pl, bool lapse)
{
	dlg_state st = dlg_get_state();
	bool busy = st != DS_IDLE;
	float rate = lapse && !busy && SIM.place >= 0 && PLACE[SIM.place].seat >= 0 ? TIMELAPSE : 1.0f;
	SIM.hour += rate / SECONDS_PER_HOUR;
	SIM.real += 1.0f;
	SIM.day_real[SIM.day] += 1.0f;
	s_wake += 1.0f;

	/* The world raises what story_update would. */
	if (SIM.place == P_SPRING) dlg_set_flag(FLAG_AT_SPRING, true);
	if (SIM.place == P_CABIN) dlg_set_flag(FLAG_CABIN_ENTERED, true);
	if (SIM.day == NUM_DAYS && SIM.hour > 12.2f) dlg_set_flag(FLAG_RAIN_ON, true);
	if (SIM.elk_t >= 0.0f && (SIM.elk_t += 1.0f) > ELK_SECONDS && !dlg_flag(FLAG_ELK_DONE)) {
		dlg_set_flag(FLAG_ELK_DONE, true);
		SIM.want_call = pl->answers;
	}

	dlg_world w = world(s_wake > 5.0f);
	dlg_tick(&w, 1.0f);
	st = dlg_get_state();

	switch (st) {
	case DS_RINGING:
		s_ring_t += 1.0f;
		if (pl->answers && s_ring_t >= pl->answer_delay) {
			dlg_answer(&w);
			s_ring_t = 0.0f;
			s_wait = 0.0f;
		} else if (s_ring_t >= RING_GIVE_UP) {
			dlg_unanswered(&w);
			s_ring_t = 0.0f;
		}
		break;
	case DS_LINE: {
		int spk;
		const char *line = dlg_line(&spk);
		s_wait += 1.0f;
		if (s_wait >= voice_line_seconds(line, (speaker_t)spk) + LINE_GAP) {
			s_wait = 0.0f;
			dlg_next();
		}
		break;
	}
	case DS_CHOICE: {
		s_wait += 1.0f;
		if (s_wait >= 4.0f) {
			const char *opts[4];
			int n = dlg_options(opts, 4);
			s_wait = 0.0f;
			dlg_choose(pl->random ? (int)(rnd() % n) : 0);
		}
		break;
	}
	case DS_CLOSING:
		dlg_close();
		s_wait = 0.0f;
		break;
	default:
		s_ring_t = 0.0f;
		if (SIM.want_call) {
			SIM.want_call = false;
			dlg_answer(&w);
		}
		break;
	}
}

/* Walk somewhere: real time passes, the radio keeps working. */
static void go(const player_t *pl, int to)
{
	float s = walk_seconds(SIM.place, to);
	if (s <= 0.0f) {
		SIM.place = to;
		return;
	}
	SIM.place = -1;
	int from_area_place = to;
	for (float t = 0.0f; t < s; t += 1.0f) second(pl, false);
	SIM.place = from_area_place;
}

static void finish_calls(const player_t *pl)
{
	for (int guard = 0; guard < 2000 && dlg_get_state() != DS_IDLE && dlg_get_state() != DS_RINGING; guard++)
		second(pl, false);
}

typedef struct {
	bool ended;
	float hours;
	int missed_story;
	char missed[400];
} result_t;

static result_t play(const player_t *pl)
{
	memset(&SIM, 0, sizeof(SIM));
	SIM.elk_t = -1.0f;
	SIM.rng = pl->seed * 2654435761u + 1u;
	s_ring_t = s_wait = 0.0f;
	dlg_init();
	result_t res;
	memset(&res, 0, sizeof(res));

	for (int day = 1; day <= NUM_DAYS && !SIM.ended; day++) {
		new_day(day);
		s_wake = 0.0f;
		const step_t *plan = pl->plan + day * pl->stride;
		bool slept = false;
		for (int k = 0; plan[k].hour >= 0.0f && !slept && !SIM.ended; k++) {
			const step_t *s = &plan[k];
			if (pl->random && s->act != DO_SLEEP && s->act != DO_GATE && frand() < 0.25f) continue;
			/* Wait for the step's time, where we are. */
			for (int guard = 0; SIM.hour < s->hour && guard < 40000; guard++) second(pl, false);
			go(pl, s->place);
			switch (s->act) {
			case DO_WAIT:
			case DO_LAPSE:
				for (int guard = 0; SIM.hour < s->until && guard < 40000; guard++) second(pl, s->act == DO_LAPSE);
				break;
			case DO_RAISE: dlg_set_flag(s->flag, true); break;
			case DO_READ_AM: dlg_set_flag(FLAG_OBS_AM, true); break;
			case DO_READ_PM: dlg_set_flag(FLAG_OBS_PM, true); break;
			case DO_CALL:
				finish_calls(pl);
				if (pl->answers && dlg_get_state() == DS_IDLE) {
					dlg_world w = world(true);
					dlg_answer(&w);
				}
				break;
			case DO_SLEEP:
				finish_calls(pl);
				/* The cot works from seven; a call in progress plays out first. */
				for (int guard = 0; SIM.hour < 19.0f && guard < 40000; guard++) second(pl, false);
				slept = true;
				break;
			case DO_GATE:
				for (int guard = 0; !can_leave() && guard < 40000; guard++) second(pl, true);
				SIM.ended = can_leave();
				break;
			}
		}
		finish_calls(pl);
		CHECK(SIM.hour < 30.0f, "%s: day %d ran on to %.1f", pl->name, day, SIM.hour);
	}
	res.ended = SIM.ended;
	res.hours = SIM.real / 3600.0f;
	return res;
}

/* Calls the story path must have played. */
static const char *MUST[] = {
	"D1_ARRIVE", "D1_PM", "D1_NIGHT", "D2_AM", "D2_SPRING", "D2_FAWNS", "D2_EVE", "D2_PM", "D3_AM", "D3_SUPPLY",
	"D3_WIND", "D3_CABIN", "D3_EVE", "D4_AM", "D4_GAUGE", "D4_SOUND", "D4_CAIRN", "D4_EVE", "D5_AM", "D5_STORM",
	"D5_REPORT", "D5_EVE", "D5_NIGHT", "D6_AM", "D6_RELAY", "D6_EVE", "D6_NIGHT", "D7_AM", "D7_BENCH", "D7_SEVEN",
	"D7_NIGHT", "D8_AM", "D8_DUSK", "D8_COUNT", "D8_RUTH", "D8_EVE", "D9_AM", "D9_POSTCARD", "D9_EVE", "D9_PM",
	"D9_NIGHT", "D10_AM", "D10_CABIN", "D10_CHORES", "D10_EVE", "D10_NIGHT", "D11_AM", "D11_WHERE", "D11_LAST",
};

static int find_call(const char *id)
{
	for (int c = 0; c < DLG_NUM_CALLS; c++)
		if (!strcmp(DLG_CALLS[c].id, id)) return c;
	return -1;
}

static void test_story_path(void)
{
	player_t pl = { "story", &STORY[0][0], 24, true, 6.0f, false, 1 };
	result_t r = play(&pl);
	CHECK(r.ended, "the story path never reached the gate");
	CHECK(SIM.season_over, "the story path never heard the last call");
	CHECK(!SIM.bad_line, "a line went out with a raw token: %s", SIM.last_line);
	char missed[600] = "";
	for (unsigned i = 0; i < sizeof(MUST) / sizeof(MUST[0]); i++) {
		int c = find_call(MUST[i]);
		CHECK(c >= 0, "no call %s", MUST[i]);
		if (c >= 0 && !dlg_call_done(c)) {
			strcat(missed, " ");
			strcat(missed, MUST[i]);
		}
	}
	CHECK(!missed[0], "the story path missed:%s", missed);
	printf("  story path: %.2f h real (%d calls, %d lines, ~%d words)\n", r.hours, SIM.calls_played, SIM.lines,
	       SIM.words + SIM.lines);
	printf("   by day (min):");
	for (int d = 1; d <= NUM_DAYS; d++) printf(" %.0f", SIM.day_real[d] / 60.0f);
	printf("\n");
	CHECK(r.hours >= 4.0f && r.hours <= 6.0f, "the story path took %.2f h, outside 4-6 h", r.hours);
}

static void test_ignore_everything(void)
{
	player_t pl = { "ignore", &IGNORE[0][0], 4, false, 0.0f, false, 2 };
	result_t r = play(&pl);
	CHECK(r.ended, "ignoring everything never reached the gate");
	CHECK(!SIM.season_over, "the last call played without being answered");
	CHECK(SIM.calls_played == 0, "%d calls played without being answered", SIM.calls_played);
	printf("  ignore everything: %.2f h real, still ends at the gate\n", r.hours);
}

static void test_random_players(void)
{
	int ended = 0, runs = 60;
	float lo = 1e9f, hi = 0.0f;
	for (int i = 0; i < runs; i++) {
		player_t pl = { "random", &STORY[0][0], 24, true, 0.0f, true, 100u + i };
		pl.answers = (i % 7) != 3;                        /* one in seven never answers */
		pl.answer_delay = 2.0f + (i * 13 % 50);           /* some are slower than the ring */
		result_t r = play(&pl);
		ended += r.ended;
		CHECK(!SIM.bad_line, "seed %u: a line went out with a raw token: %s", pl.seed, SIM.last_line);
		lo = fminf(lo, r.hours);
		hi = fmaxf(hi, r.hours);
	}
	CHECK(ended == runs, "%d of %d randomised seasons never reached the gate", runs - ended, runs);
	printf("  %d randomised seasons: all end; %.2f-%.2f h real\n", runs, lo, hi);
}

int main(void)
{
	printf("Story tests\n");
	test_script();
	test_story_path();
	test_ignore_everything();
	test_random_players();
	printf("%d checks, %d failed\n", s_checks, s_fails);
	return s_fails ? 1 : 0;
}
