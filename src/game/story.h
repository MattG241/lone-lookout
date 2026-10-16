/*
 * story.h -- the bridge between the radio script and the game.
 *
 * The game raises story flags when things happen (a note read, the spring
 * reached, the smoke sighted); the script's conditions read them. The
 * script's "do" statements land here as errands, the elk, the end of the
 * season. Day 5's smoke and Day 8's bugling bulls live here too, as do the
 * doe and fawns.
 */
#ifndef LL_STORY_H
#define LL_STORY_H

#include <stdbool.h>
#include "core/mathx.h"
#include "game/dialogue.h"

void story_init(void);
void story_new_day(int day);
void story_update(float dt);
dlg_world story_world(void);

void story_raise(int flag);
bool story_flag(int flag);
void story_note_read(int note);
void story_readings_taken(bool morning);

const char *story_errand(void);              /* today's errand, or NULL */
bool story_errand_done(void);
bool story_can_leave(void);                  /* the gate is the way out now */

/* Day 5: the lightning smoke on Harrow Ridge. */
bool story_smoke(v3 *base, float *height, float *alpha);
float story_smoke_bearing(void);             /* degrees from the tower */
void story_finder_sight(float yaw);          /* A in the fire finder */

/* Saving: the errand, the season's end, readings, the smoke's bearing. */
void story_save(sw_t *w);
void story_load(sr_t *r);

/* The doe and twin fawns, mornings in the meadow. */
#define DEER_COUNT 3
bool story_deer(int i, v3 *pos, float *facing, float *alpha, float *scale);

#endif
