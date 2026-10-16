/*
 * vo.h -- recorded radio lines from the SD card, in place of the murmur.
 *
 * If sd:/apps/lonelookout/vo/ exists, each radio line looks for
 * <LINE_ID>.pcm there: raw 16-bit little-endian mono at 22,050 Hz, no
 * header. LINE_ID is the call id and the line's number within the call,
 * e.g. D7_SEVEN_03 (gen/vo_lines.txt lists them all, with the text). Lines
 * that read out live values ({dry}, {wind}...) always use the murmur.
 * Files load on a background thread so the game never waits on the card.
 */
#ifndef LL_VO_H
#define LL_VO_H

#include <gctypes.h>
#include <stdbool.h>

typedef enum { VO_NONE, VO_PENDING, VO_READY, VO_MISSING } vo_status;

void vo_init(void);                          /* starts the loader; harmless without a card */
bool vo_available(void);                     /* the vo folder was found */
void vo_request(const char *line_id);        /* replaces any earlier request */
vo_status vo_poll(const s16 **pcm, int *len);
void vo_debug(char *out, int n);             /* a line for the dev overlay */

#endif
