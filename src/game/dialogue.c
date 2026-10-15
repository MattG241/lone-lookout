/*
 * dialogue.c -- run the compiled radio script.
 *
 * Calls ring on their own when their conditions hold (day, time window,
 * where the player is, story flags, other calls done), story calls before
 * reports before small talk. Unanswered calls try again later, until their
 * retry time. When the player keys the radio with nothing ringing, an
 * "initiated" call that is due plays if there is one, otherwise a check-in
 * from the pool.
 */
#include <stdio.h>
#include <string.h>
#include "game/dialogue.h"
#include "dialogue_data.h"

#define MAX_OPTIONS 4
#define RETRY_GAP_H 0.75f            /* game hours between attempts (90 s real) */
#define QUIET_AFTER_CALL 10.0f       /* seconds of radio silence after a call */

static struct {
	dlg_state state;
	int call;
	int pc;                          /* op index of the current line or choice */
	u8 flags[(DLG_NUM_FLAGS + 7) / 8 + 1];
	u8 done[DLG_NUM_CALLS];          /* 0 not yet, 1 played, 2 missed */
	float next_try[DLG_NUM_CALLS];   /* game hour (with day) of the next attempt */
	s8 used_day[DLG_NUM_CALLS];      /* check-ins: day last used */
	float quiet;
	char line[400];
	char opt[MAX_OPTIONS][200];
	int opt_op[MAX_OPTIONS];
	int nopt;
	int day;
} D;

int dlg_num_calls(void) { return DLG_NUM_CALLS; }
const char *dlg_call_id(int c) { return DLG_CALLS[c].id; }
int dlg_call_kind(int c) { return DLG_CALLS[c].kind; }
int dlg_call_day(int c) { return DLG_CALLS[c].day_min; }

void dlg_init(void)
{
	memset(&D, 0, sizeof(D));
	for (int i = 0; i < DLG_NUM_CALLS; i++) D.used_day[i] = -1;
	D.call = -1;
}

bool dlg_flag(int f) { return f >= 0 && f < DLG_NUM_FLAGS && (D.flags[f >> 3] >> (f & 7)) & 1; }

void dlg_set_flag(int f, bool v)
{
	if (f < 0 || f >= DLG_NUM_FLAGS) return;
	if (v) D.flags[f >> 3] |= (u8)(1u << (f & 7));
	else D.flags[f >> 3] &= (u8)~(1u << (f & 7));
}

bool dlg_call_done(int c) { return c >= 0 && c < DLG_NUM_CALLS && D.done[c] == 1; }
dlg_state dlg_get_state(void) { return D.state; }
int dlg_current_call(void) { return D.call; }
bool dlg_current_garbled(void) { return D.call >= 0 && DLG_CALLS[D.call].garble; }

/* ------------------------------------------------------------------------ */

static int cur_area;

static bool cond_ok(int cond)
{
	if (cond == 0) return true;
	bool neg = cond < 0;
	int c = neg ? -cond : cond;
	bool v = c >= DLG_COND_AREA ? cur_area == c - DLG_COND_AREA : dlg_flag(c - 1);
	return neg ? !v : v;
}

/* Fill {tokens} from the game. */
static void expand(const char *src, char *dst, int n)
{
	int o = 0;
	while (*src && o < n - 1) {
		if (*src == '{') {
			const char *e = strchr(src, '}');
			if (e && e - src < 32) {
				char name[32], val[64];
				int len = (int)(e - src - 1);
				memcpy(name, src + 1, len);
				name[len] = 0;
				val[0] = 0;
				dlg_hook_token(name, val, sizeof(val));
				for (const char *v = val; *v && o < n - 1; v++) dst[o++] = *v;
				src = e + 1;
				continue;
			}
		}
		dst[o++] = *src++;
	}
	dst[o] = 0;
}

static float abs_hour(const dlg_world *w)
{
	/* After midnight still belongs to the evening before. */
	return w->hour < 5.0f ? w->hour + 24.0f : w->hour;
}

static bool where_ok(const dlg_call *c, const dlg_world *w)
{
	if (abs_hour(w) * 60.0f >= c->where_until) return true;
	switch (c->where_kind) {
	case DLG_WHERE_AREA: return w->area == c->where_arg;
	case DLG_WHERE_CAB: return w->indoor == 1;
	case DLG_WHERE_CABIN: return w->indoor == 2;
	case DLG_WHERE_SEAT: return w->seat == c->where_arg;
	default: return true;
	}
}

static bool eligible(int i, const dlg_world *w, bool check_time)
{
	const dlg_call *c = &DLG_CALLS[i];
	if (w->day < c->day_min || w->day > c->day_max) return false;
	float h = abs_hour(w);
	int m = (int)(h * 60.0f);
	if (check_time && (m < c->t_from || m >= c->t_retry)) return false;
	if (!where_ok(c, w)) return false;
	cur_area = w->area;
	for (int k = 0; k < 4; k++)
		if (!cond_ok(c->needs[k])) return false;
	for (int k = 0; k < 2; k++)
		if (c->after[k] >= 0 && D.done[c->after[k]] != 1) return false;
	return true;
}

static void finish_call(void)
{
	int c = D.call;
	D.state = DS_CLOSING;
	if (c >= 0) {
		if (DLG_CALLS[c].kind != DLG_KIND_CHECKIN) D.done[c] = 1;
		dlg_hook_call_end(c);
	}
}

/* Run non-speaking ops until a line, a choice or the end. */
static void run(void)
{
	for (int guard = 0; guard < 256; guard++) {
		const dlg_op *o = &DLG_OPS[D.pc];
		switch (o->op) {
		case DLG_LINE:
			expand(DLG_STRINGS[o->str], D.line, sizeof(D.line));
			D.state = DS_LINE;
			dlg_hook_line(o->a, D.line);
			return;
		case DLG_CHOICE: {
			D.nopt = 0;
			for (int k = 0; k < o->a; k++) {
				const dlg_op *opt = &DLG_OPS[D.pc + 1 + k];
				if (!cond_ok(opt->cond) || D.nopt >= MAX_OPTIONS) continue;
				expand(DLG_STRINGS[opt->str], D.opt[D.nopt], sizeof(D.opt[0]));
				D.opt_op[D.nopt++] = D.pc + 1 + k;
			}
			if (D.nopt == 0) {          /* nothing to say: skip past the options */
				D.pc += 1 + o->a;
				continue;
			}
			D.state = DS_CHOICE;
			return;
		}
		case DLG_GOTO:
			D.pc = o->target;
			continue;
		case DLG_BRANCH:
			D.pc = cond_ok(o->cond) ? o->target : D.pc + 1;
			continue;
		case DLG_SET:
			dlg_set_flag(o->set, true);
			D.pc++;
			continue;
		case DLG_CLEAR:
			dlg_set_flag(o->set, false);
			D.pc++;
			continue;
		case DLG_DO:
			dlg_hook_action(o->a, DLG_STRINGS[o->str]);
			D.pc++;
			continue;
		case DLG_OPTION:            /* fell into an option list: shouldn't happen */
		case DLG_END:
		default:
			finish_call();
			return;
		}
	}
	finish_call();
}

static void start(int c, const dlg_world *w)
{
	D.call = c;
	D.pc = DLG_CALLS[c].first;
	D.day = w->day;
	cur_area = w->area;
	run();
}

void dlg_tick(const dlg_world *w, float dt)
{
	cur_area = w->area;
	if (D.state == DS_IDLE) D.quiet += dt;
	if (D.state != DS_IDLE || !w->can_ring || D.quiet < QUIET_AFTER_CALL) return;
	float h = abs_hour(w);
	float now = w->day * 48.0f + h;
	int best = -1;
	for (int i = 0; i < DLG_NUM_CALLS; i++) {
		const dlg_call *c = &DLG_CALLS[i];
		if (c->kind == DLG_KIND_INITIATED || c->kind == DLG_KIND_CHECKIN || D.done[i]) continue;
		/* Calls whose day has passed, or whose retry time is gone, are missed. */
		if (w->day > c->day_max || (w->day == c->day_max && h * 60.0f >= c->t_retry)) {
			D.done[i] = 2;
			continue;
		}
		if (now < D.next_try[i]) continue;
		if (!eligible(i, w, true)) continue;
		if (best < 0 || c->kind < DLG_CALLS[best].kind) best = i;
		if (c->kind == DLG_KIND_STORY) break;
	}
	if (best >= 0) {
		D.call = best;
		D.state = DS_RINGING;
	}
}

void dlg_unanswered(const dlg_world *w)
{
	if (D.state != DS_RINGING) return;
	int c = D.call;
	D.next_try[c] = w->day * 48.0f + abs_hour(w) + RETRY_GAP_H;
	if (DLG_CALLS[c].kind == DLG_KIND_SMALL) D.done[c] = 2;       /* small talk isn't repeated */
	D.state = DS_IDLE;
	D.quiet = 0.0f;
	D.call = -1;
}

bool dlg_answer(const dlg_world *w)
{
	cur_area = w->area;
	if (D.state == DS_RINGING) {
		start(D.call, w);
		return true;
	}
	if (D.state != DS_IDLE) return false;
	/* Calling Basin: something Theo has to report first, else a check-in. */
	for (int i = 0; i < DLG_NUM_CALLS; i++)
		if (DLG_CALLS[i].kind == DLG_KIND_INITIATED && !D.done[i] && eligible(i, w, true)) {
			start(i, w);
			return true;
		}
	int pick = -1;
	for (int i = 0; i < DLG_NUM_CALLS; i++) {
		if (DLG_CALLS[i].kind != DLG_KIND_CHECKIN || !eligible(i, w, true)) continue;
		if (D.used_day[i] == w->day) continue;
		if (pick < 0 || D.used_day[i] < D.used_day[pick]) pick = i;   /* least recently used */
	}
	if (pick < 0) {
		/* Everything's been said today: the plainest check-in, again. */
		for (int i = DLG_NUM_CALLS - 1; i >= 0; i--)
			if (DLG_CALLS[i].kind == DLG_KIND_CHECKIN && eligible(i, w, true)) {
				pick = i;
				break;
			}
	}
	if (pick < 0) return false;
	D.used_day[pick] = (s8)w->day;
	start(pick, w);
	return true;
}

const char *dlg_line(int *speaker)
{
	if (speaker) *speaker = DLG_OPS[D.pc].a;
	return D.line;
}

bool dlg_line_id(char *out, int n)
{
	if (D.state != DS_LINE || D.call < 0) return false;
	const dlg_op *o = &DLG_OPS[D.pc];
	if (strchr(DLG_STRINGS[o->str], '{')) return false;
	int k = 0;
	for (int pc = DLG_CALLS[D.call].first; pc < D.pc; pc++)
		if (DLG_OPS[pc].op == DLG_LINE) k++;
	snprintf(out, n, "%s_%02d", DLG_CALLS[D.call].id, k + 1);
	return true;
}

int dlg_options(const char **texts, int max)
{
	int n = D.nopt < max ? D.nopt : max;
	for (int i = 0; i < n; i++) texts[i] = D.opt[i];
	return n;
}

void dlg_next(void)
{
	if (D.state != DS_LINE) return;
	D.pc++;
	run();
}

void dlg_choose(int i)
{
	if (D.state != DS_CHOICE || i < 0 || i >= D.nopt) return;
	const dlg_op *o = &DLG_OPS[D.opt_op[i]];
	if (o->set >= 0) dlg_set_flag(o->set, true);
	D.pc = o->target;
	run();
}

void dlg_close(void)
{
	if (D.state != DS_CLOSING) return;
	D.state = DS_IDLE;
	D.call = -1;
	D.quiet = 0.0f;
}

void dlg_force_call(int c)
{
	dlg_world w = { DLG_CALLS[c].day_min, DLG_CALLS[c].t_from / 60.0f, -1, 0, -1, true };
	D.state = DS_IDLE;
	start(c, &w);
}
