/* The field guide: optional destinations and discoveries, saved with the season. */
#ifndef LL_GUIDE_H
#define LL_GUIDE_H

#include "core/mathx.h"
#include "core/saveio.h"

#define GUIDE_PLACES 8
#define GUIDE_MOMENTS 8
typedef struct {
	const char *name, *detail, *best;
	float x, z, radius;
} guide_place;

typedef struct {
	u32 visited;
	int selected, pin;
	u8 moments[GUIDE_MOMENTS];
} guide_state;
extern guide_state GUIDE;

void guide_init(void);
guide_place guide_get(int index);
int guide_visit(v3 feet);                  /* newly discovered place, or -1 */
int guide_count(void);
void guide_select(int delta);
void guide_toggle_pin(void);
bool guide_bearing(v3 feet, float yaw, float *distance, float *angle);
const char *guide_moment_name(int index);
const char *guide_moment_note(int index);
const char *guide_moment_hint(int index);
bool guide_keep(int index, int day);
int guide_moments(void);
void guide_save(sw_t *w);
bool guide_load(sr_t *r);                  /* optional extension; 1.0/1.1 saves work */

#endif
