/*
 * listen.h -- Listen (hold C; hold - without a Nunchuk) and the sound log.
 *
 * Twelve sounds live in the world at their places and hours (DESIGN.md
 * section 7, S01-S12). Every frame this module works out how loud each one
 * is where Theo stands and tells the mixer. While listening the world
 * quietens, the sounds he can hear get handwritten labels, and holding on
 * one for three seconds writes it into the notebook.
 */
#ifndef LL_LISTEN_H
#define LL_LISTEN_H

#include <stdbool.h>
#include "core/mathx.h"
#include "audio/ambient.h"

void listen_init(void);
void listen_update(float dt);
void listen_draw(void);
float listen_amount(void);                    /* 0..1, eased */
/* One-shot sounds that can be logged while they ring: the stair, the elk, the chimes. */
void listen_event(int sound, v3 pos);

void listen_debug_force(bool on);            /* screenshot tour */

const char *sound_name(int id);
const char *sound_note(int id);               /* what Theo wrote down */
int sounds_logged(void);

#endif
