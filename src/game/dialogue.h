/*
 * dialogue.h -- the radio script's runtime: which call rings when, and how
 * a call plays through its lines, choices, flags and actions.
 *
 * The script is written in data/dialogue/ (.lls files) and compiled into tables
 * (tools/dialogue/compile_lls.py). This module is pure logic -- no drawing,
 * no audio, no input -- so the host tests can play the whole season. The
 * radio UI (radio.c) drives it and the game answers its questions through
 * the dlg_hook_* functions.
 */
#ifndef LL_DIALOGUE_H
#define LL_DIALOGUE_H

#include <gctypes.h>
#include <stdbool.h>

typedef enum { DLG_LINE, DLG_CHOICE, DLG_OPTION, DLG_GOTO, DLG_BRANCH, DLG_SET, DLG_CLEAR, DLG_DO, DLG_END } dlg_opcode;

typedef struct {
	u8 op;
	u8 a;            /* LINE: speaker; CHOICE: option count; DO: action */
	s16 cond;        /* OPTION/BRANCH condition (0 = always) */
	s16 set;         /* OPTION/SET/CLEAR: flag (-1 = none) */
	u16 str;         /* LINE/OPTION text; DO argument */
	u16 target;      /* OPTION/GOTO/BRANCH: op index */
	u16 pad;
} dlg_op;

enum { DLG_KIND_STORY, DLG_KIND_REPORT, DLG_KIND_SMALL, DLG_KIND_INITIATED, DLG_KIND_CHECKIN };
enum { DLG_WHERE_ANY, DLG_WHERE_AREA, DLG_WHERE_CAB, DLG_WHERE_CABIN, DLG_WHERE_SEAT };

typedef struct {
	const char *id;
	u16 first;
	u8 day_min, day_max;
	u16 t_from, t_to, t_retry;       /* minutes since midnight (may run past 24:00) */
	u8 where_kind, where_arg;
	u16 where_until;                 /* after this time the "where" no longer matters */
	s16 needs[4];                    /* flag+1 must hold; -(flag+1) must not; area conditions too */
	s16 after[2];                    /* calls that must be done (-1 none) */
	u8 kind, garble;
} dlg_call;

#include "dialogue_flags.h"

typedef enum { DS_IDLE, DS_RINGING, DS_LINE, DS_CHOICE, DS_CLOSING } dlg_state;
typedef enum { SPK_DOT, SPK_THEO, SPK_CREW } dlg_speaker;

/* What the script can see of the world this moment. */
typedef struct {
	int day;
	float hour;
	int area;                 /* world_area or -1 */
	int indoor;               /* indoor_t */
	int seat;                 /* sit spot id, or -1 */
	bool can_ring;            /* not asleep, not in a menu that forbids it */
} dlg_world;

void dlg_init(void);
/* Ring whatever is due (call at least once a second). */
void dlg_tick(const dlg_world *w, float dt);
/* The player keys the radio: answer a ringing call, or call Basin. */
bool dlg_answer(const dlg_world *w);
/* A call is ringing unanswered for too long: it will try again later. */
void dlg_unanswered(const dlg_world *w);

dlg_state dlg_get_state(void);
int dlg_current_call(void);
bool dlg_current_garbled(void);
/* The line being spoken (tokens filled in). */
const char *dlg_line(int *speaker);
/* Its stable id for recorded voice-over, e.g. "D7_SEVEN_03" (the call and the
 * line's number in it). False for lines with live values in them. */
bool dlg_line_id(char *out, int n);
/* Visible choices (tokens filled in); returns the count. */
int dlg_options(const char **texts, int max);
void dlg_next(void);                     /* the current line is over */
void dlg_choose(int visible_index);
void dlg_close(void);                    /* closing squelch done */

bool dlg_flag(int f);
void dlg_set_flag(int f, bool v);
bool dlg_call_done(int c);
void dlg_force_call(int c);              /* screenshot tour and tests */

/* Provided by the game (game/story.c) or by the tests. */
void dlg_hook_line(int speaker, const char *text);     /* a line starts: voice it, log it */
void dlg_hook_action(int action, const char *arg);     /* "do" statements */
void dlg_hook_token(const char *name, char *out, int n);
void dlg_hook_call_end(int call);

/* Script data accessors (for tests and the notebook). */
int dlg_num_calls(void);
const char *dlg_call_id(int c);
int dlg_call_kind(int c);
int dlg_call_day(int c);

#endif
