/*
 * playtest.h -- an optional play log on the SD card, for playtests.
 *
 * With "Playtest log" on in Options (off by default) and a card in the slot,
 * the game appends one tab-separated line per event to
 * sd:/apps/lonelookout/playtest.log: seconds since this sitting's first
 * event, the day, the in-game time, the event and its details. Where you are is noted once
 * a minute, and so is every stretch of half a minute or more without input.
 * tools/playtest/summarise.py turns a log into a report: time per day, the
 * calls and how long they rang, where people stood still, when they quit.
 * Nothing leaves the card.
 */
#ifndef LL_PLAYTEST_H
#define LL_PLAYTEST_H

#include <stdbool.h>

void playtest_event(const char *event, const char *fmt, ...);   /* fmt: the details, or NULL */
void playtest_update(float dt);       /* in play: the once-a-minute note and idle stretches */
void playtest_flush(void);            /* write what's buffered to the card */
bool playtest_on(void);

#endif
