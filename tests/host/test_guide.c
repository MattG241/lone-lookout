#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game/guide.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

int main(void)
{
	guide_init();
	float distance, angle;
	CHECK(!guide_bearing(v3_make(0, 0, 0), 0, &distance, &angle));
	CHECK(guide_count() == 0 && GUIDE.pin == -1);
	guide_select(-1);
	CHECK(GUIDE.selected == GUIDE_PLACES - 1);
	guide_select(1);
	CHECK(GUIDE.selected == 0);
	guide_toggle_pin();
	guide_place tower = guide_get(0);
	CHECK(guide_bearing(v3_make(tower.x, 0, tower.z + 50), 0, &distance, &angle));
	CHECK(fabsf(distance - 50) < 0.001f && fabsf(angle) < 0.001f);
	guide_bearing(v3_make(tower.x - 50, 0, tower.z), 0, &distance, &angle);
	CHECK(fabsf(angle - LL_PI * 0.5f) < 0.001f);
	guide_toggle_pin();
	CHECK(GUIDE.pin == -1);
	for (int i = 0; i < GUIDE_PLACES; i++) {
		guide_place p = guide_get(i);
		CHECK(strlen(p.name) && strlen(p.best) && p.x > 0 && p.z > 0);
		CHECK(guide_visit(v3_make(p.x, 0, p.z)) == i);
		CHECK(guide_visit(v3_make(p.x, 0, p.z)) == -1);
	}
	CHECK(guide_count() == GUIDE_PLACES);
	GUIDE.selected = 3;
	guide_toggle_pin();
	CHECK(guide_keep(0, 2));
	CHECK(!guide_keep(0, 3));
	CHECK(!guide_keep(-1, 3) && !guide_keep(GUIDE_MOMENTS, 3));
	CHECK(!guide_keep(2, 0) && !guide_keep(2, 12));
	CHECK(guide_keep(7, 11) && guide_moments() == 2);
	u8 data[64];
	sw_t w;
	sw_init(&w, data, sizeof(data));
	guide_save(&w);
	guide_init();
	sr_t r;
	sr_init(&r, data, w.len);
	CHECK(guide_load(&r) && !r.bad && r.pos == w.len);
	CHECK(guide_count() == GUIDE_PLACES && GUIDE.selected == 3 && GUIDE.pin == 3);
	CHECK(GUIDE.moments[0] == 2 && GUIDE.moments[7] == 11 && guide_moments() == 2);
	/* A v1.1 save ends before this extension. An empty extension is valid. */
	sr_init(&r, data, 0);
	CHECK(guide_load(&r) && !r.bad && GUIDE.pin == -1 && !GUIDE.visited);
	for (u32 cut = 1; cut < w.len; cut++) {
		sr_init(&r, data, cut);
		CHECK(!guide_load(&r));
	}
	data[0] = 0;
	sr_init(&r, data, w.len);
	CHECK(!guide_load(&r));
	printf("Field guide: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
