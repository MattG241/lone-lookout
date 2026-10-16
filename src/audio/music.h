/*
 * music.h -- the music sequencer and its instruments (mixer thread).
 *
 * The six pieces are scores in data/music/, compiled into tables by
 * tools/music/compile_music.py. The sequencer plays one piece at a time,
 * dropping or swapping the bars the score allows, nudging timing and touch,
 * so it never plays exactly the same way twice. Instruments are synthesised:
 * a Karplus-Strong guitar with a small body, guitar harmonics, an additive
 * felt piano, a low drone and a very quiet pad, through a little reverb.
 *
 * Pure C with no Wii dependencies, so the host tests can render it.
 */
#ifndef LL_MUSIC_H
#define LL_MUSIC_H

#include <gctypes.h>
#include <stdbool.h>

typedef struct { u16 start, dur; u8 note, voice, vel; } mus_event;   /* start, dur in 1/MUS_TICKS beat */
typedef struct { u16 first, count; u8 beats, flags; } mus_bar;
typedef struct { const char *id, *title; float tempo; u16 first_bar, nbars; } mus_piece;

enum { MV_GUITAR, MV_HARMONIC, MV_PIANO, MV_DRONE, MV_PAD };

int music_find(const char *id);              /* piece index, or -1 (any thread) */
int music_count(void);
const char *music_title(int piece);

/* Mixer thread only. */
void music_init(unsigned seed);
void music_play(int piece, bool resolve);    /* resolve: include the coda (the credits) */
void music_stop(float fade_seconds);
bool music_active(void);                     /* a piece is playing or still ringing out */
/* Add n samples of music into L and R, scaled by gain. */
void music_render(float *L, float *R, int n, float gain);
/* Tests: sound one note now on an instrument (MV_*), outside any piece. */
void music_debug_note(int voice, int note, float vel, float seconds);

#endif
