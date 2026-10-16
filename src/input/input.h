/*
 * input.h -- Wii Remote + Nunchuk, reduced to what the game asks for.
 *
 * Buttons are reported as libogc WPAD bit masks (WPAD_BUTTON_*,
 * WPAD_NUNCHUK_BUTTON_*). Pointer coordinates are normalised to [-1, 1] with
 * +x right and +y down; stick is [-1, 1] with +y forward.
 */
#ifndef LL_INPUT_H
#define LL_INPUT_H

#include <gctypes.h>
#include <stdbool.h>

typedef struct {
	bool connected;
	bool nunchuk;
	u32 held, down, up;
	float stick_x, stick_y;
	bool ir_valid;
	float ir_x, ir_y;          /* smoothed */
	float roll_deg;            /* remote roll, smoothed */
} input_t;

extern input_t IN;

void input_init(void);
void input_poll(void);
/* Edge-triggered presses are seen by exactly one simulation step: call after
 * the first fixed step of a frame. */
static inline void input_consume(void) { IN.down = IN.up = 0; }
static inline bool in_down(u32 mask) { return (IN.down & mask) != 0; }
static inline bool in_held(u32 mask) { return (IN.held & mask) != 0; }
/* A press that one handler acts on, so no later handler in the same step sees it. */
static inline bool in_take(u32 mask)
{
	bool d = (IN.down & mask) != 0;
	IN.down &= ~mask;
	return d;
}

#endif
