/*
 * ambient.h -- the sounds worth keeping: the sound log's emitters, each
 * synthesised in the mixer thread at the gain and pan the game sets every
 * frame (DESIGN.md section 7, S01-S12).
 *
 * Two of them are events rather than beds: the singing stair and the elk
 * play as one-shot sounds (audio_ui), and only need an entry here so Listen
 * can find them.
 */
#ifndef LL_AMBIENT_H
#define LL_AMBIENT_H

#include <stdbool.h>

typedef enum {
	SND_STAIR,          /* S01 the third stair (event) */
	SND_CREEK,          /* S02 creek over stones */
	SND_GRASSHOPPERS,   /* S03 meadow, midday */
	SND_ELK,            /* S04 elk bugle (event) */
	SND_SANDPIPER,      /* S05 tarn shore, morning */
	SND_FROGS,          /* S06 chorus frogs, tarn at night */
	SND_PIKA,           /* S07 ridge scree, by day */
	SND_SNAGS,          /* S08 wind in the snags */
	SND_WOODPECKER,     /* S09 burn, morning */
	SND_CHIMES,         /* S10 spoon chimes on the porch */
	SND_TIN_RAIN,       /* S11 rain on the cab roof */
	SND_NOTHING,        /* S12 almost nothing */
	SND_COUNT
} sound_id;

/* Mixer thread. gain[] and pan[] are targets (smoothed here); wind 0..1.5. */
void ambient_init(void);
void ambient_render(float *L, float *R, int n, const float *gain, const float *pan, float wind);

#endif
