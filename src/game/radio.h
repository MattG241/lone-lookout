/*
 * radio.h -- the radio as the player meets it: the call chirp, the handset,
 * subtitles, choice cards and the radio log. The script runs in dialogue.c,
 * compiled from the .lls files in data/dialogue/.
 */
#ifndef LL_RADIO_H
#define LL_RADIO_H

#include <stdbool.h>
#include "audio/audio.h"
#include "game/dialogue.h"

void radio_init(void);
void radio_update(float dt);
bool radio_busy(void);                /* ringing or in a call */
bool radio_in_call(void);             /* handset raised */
bool radio_wants_pointer(void);       /* choice cards are taking the pointer */
void radio_draw(void);
void radio_call_basin(void);          /* B, or the handset on the cab table */
void radio_hold_hiss(float seconds);  /* leave the carrier open after a call */

/* Everything said on the radio, for the notebook's Radio page. */
#define RADIO_LOG_MAX 40
typedef struct {
	int day;
	speaker_t who;
	const char *text;
} radio_log_entry;
int radio_log(const radio_log_entry **out);           /* oldest first */
void radio_log_save(sw_t *w);
void radio_log_load(sr_t *r);                          /* after radio_init */

/* Screenshot tour: start a call and skip ahead a number of lines. */
void radio_debug_call(int call, int lines);

#endif
