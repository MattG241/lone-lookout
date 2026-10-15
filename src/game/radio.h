/*
 * radio.h -- calls, lines, choices and the subtitle/choice-card UI.
 *
 * Radio v0 carries the Day 1 radio check and a small check-in you can start
 * yourself. Phase 4 replaces the hand-written tables with data
 * compiled from the .lls files in data/dialogue, keeping this runtime.
 */
#ifndef LL_RADIO_H
#define LL_RADIO_H

#include <stdbool.h>
#include "audio/audio.h"

typedef enum { CALL_D1_RADIO_CHECK, CALL_CHECK_IN, CALL_COUNT } call_id;

void radio_init(void);
void radio_update(float dt);
void radio_ring(call_id id);          /* Basin calls you */
bool radio_busy(void);                /* ringing or in a call */
bool radio_in_call(void);             /* handset raised */
bool radio_wants_pointer(void);       /* choice cards are taking the pointer */
void radio_draw(void);
void radio_call_basin(void);          /* B, or the handset on the cab table */

/* Everything said on the radio, for the notebook's Radio page. */
#define RADIO_LOG_MAX 40
typedef struct {
	int day;
	speaker_t who;
	const char *text;
} radio_log_entry;
int radio_log(const radio_log_entry **out);           /* oldest first */

/* Screenshot tour: jump straight to a node's choices with one card hovered. */
void radio_debug_choices(call_id id, int node, int hovered);
void radio_debug_line(call_id id, int node, int line, float age);

#endif
