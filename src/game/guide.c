#include <math.h>
#include "game/guide.h"
#include "world/world.h"
#include "world/sites.h"

guide_state GUIDE;

static const struct { const char *name, *note, *hint; } MOMENTS[GUIDE_MOMENTS] = {
	{ "First light", "The tarn held the whole sky. I sat on the dock and listened until the first bird started.",
	  "Sit on the tarn dock and listen between six and eight in the morning." },
	{ "Three in the meadow", "A doe and two fawns. Small enough to disappear in the grass, if you weren't looking.",
	  "Watch the doe and fawns through binoculars. Give them room." },
	{ "What came back", "Augie pressed flowers from the burn. He kept a record of everything that came back.",
	  "Read the flower book inside Pell's Cabin." },
	{ "Seven, as instructed", "The bench was warm. The valley turned gold. For a while there was nothing else to do.",
	  "Sit on the ridge at sunset, between half past six and eight. Listen." },
	{ "The burn breathing", "The trees that didn't come back still had something to say. Wind through the snags.",
	  "Write down the wind in the burn's snags." },
	{ "Under the roof", "The dry storm crossed Harrow. The cab shook, but it held. I kept watch from inside.",
	  "Watch the Day 5 storm from the safety of the lookout cab." },
	{ "First rain", "Seven weeks, then rain on tin. Dot heard it too, at the other end of the radio.",
	  "Listen to rain on the lookout's roof on the final day." },
	{ "Very small things", "Almost nothing, up on the ridge after dark. Almost. I can hear more when I stop trying.",
	  "Write down the quiet on Cinder Ridge after nine at night." },
};

const char *guide_moment_name(int i) { return i >= 0 && i < GUIDE_MOMENTS ? MOMENTS[i].name : ""; }
const char *guide_moment_note(int i) { return i >= 0 && i < GUIDE_MOMENTS ? MOMENTS[i].note : ""; }
const char *guide_moment_hint(int i) { return i >= 0 && i < GUIDE_MOMENTS ? MOMENTS[i].hint : ""; }
bool guide_keep(int i, int day)
{
	if (i < 0 || i >= GUIDE_MOMENTS || day < 1 || day > 11 || GUIDE.moments[i]) return false;
	GUIDE.moments[i] = (u8)day;
	return true;
}
int guide_moments(void)
{
	int count = 0;
	for (int i = 0; i < GUIDE_MOMENTS; i++) count += !!GUIDE.moments[i];
	return count;
}

void guide_init(void) { GUIDE = (guide_state){ .pin = -1 }; }

guide_place guide_get(int i)
{
	static const guide_place places[GUIDE_PLACES] = {
		{ "Kestrel Lookout", "Your home above the trees. The map is under the cot; binoculars hang beside the door.",
		  "Take readings before noon and after five.", TOWER_X, TOWER_Z, 24 },
		{ "Larkspur Meadow", "Follow the western trail to Augie's rain gauge, the drift fence and a good place to sit.",
		  "Doe and fawns in the morning. Grasshoppers at midday.", GAUGE_X, GAUGE_Z, 35 },
		{ "Mirror Tarn", "Follow the shore trail to the little dock. The tin beside it holds another of Augie's notes.",
		  "Sandpipers at dawn. Chorus frogs after dark.", 156.0f, DOCK_Z, 24 },
		{ "Cinder Ridge", "The northeastern trail climbs to the cairn and Augie's bench. Read the carving on its back.",
		  "Sit here at seven. Stay for the stars.", BENCH_X, BENCH_Z, 24 },
		{ "The '98 Burn", "Take the eastern loop to the tagged snag. The forest is coming back, slowly.",
		  "Woodpeckers in the morning. Wind through the snags.", TAG_SNAG_X, TAG_SNAG_Z, 34 },
		{ "Pell's Cabin", "On the trail south of the tower. A flower book, an unfinished letter and spoon chimes.",
		  "A sheltered porch when the weather turns.", CABIN_X, CABIN_Z + 4.0f, 20 },
		{ "Cold spring", "A short spur off the meadow trail. A pipe in the bank, a trough and a shared tin cup.",
		  "The creek is worth stopping to listen to.", SPRING_X, SPRING_Z, 12 },
		{ "Trailhead", "The supply box and the road gate. This is where the season began, and where it ends.",
		  "The truck brings supplies when Dot says so.", SUPPLY_X, SUPPLY_Z, 20 },
	};
	return places[i >= 0 && i < GUIDE_PLACES ? i : 0];
}

int guide_visit(v3 feet)
{
	for (int i = 0; i < GUIDE_PLACES; i++) {
		guide_place p = guide_get(i);
		float dx = feet.x - p.x, dz = feet.z - p.z;
		if (!(GUIDE.visited & (1u << i)) && dx * dx + dz * dz < p.radius * p.radius) {
			GUIDE.visited |= 1u << i;
			return i;
		}
	}
	return -1;
}

int guide_count(void)
{
	int n = 0;
	for (int i = 0; i < GUIDE_PLACES; i++) n += !!(GUIDE.visited & (1u << i));
	return n;
}

void guide_select(int delta)
{
	GUIDE.selected = ((GUIDE.selected + delta) % GUIDE_PLACES + GUIDE_PLACES) % GUIDE_PLACES;
}

void guide_toggle_pin(void) { GUIDE.pin = GUIDE.pin == GUIDE.selected ? -1 : GUIDE.selected; }

bool guide_bearing(v3 feet, float yaw, float *distance, float *angle)
{
	if (GUIDE.pin < 0 || GUIDE.pin >= GUIDE_PLACES) return false;
	guide_place p = guide_get(GUIDE.pin);
	float dx = p.x - feet.x, dz = p.z - feet.z;
	*distance = sqrtf(dx * dx + dz * dz);
	*angle = wrap_anglef(atan2f(dx, -dz) - yaw);
	return true;
}

void guide_save(sw_t *w)
{
	sw_u16(w, 0x4647);                    /* FG: append-only extension to the v1 save */
	sw_u8(w, (u8)GUIDE.visited);
	sw_u8(w, (u8)GUIDE.selected);
	sw_u8(w, (u8)(GUIDE.pin + 1));
	sw_bytes(w, GUIDE.moments, GUIDE_MOMENTS);
}

bool guide_load(sr_t *r)
{
	guide_init();
	if (r->pos == r->len) return true;    /* a season saved by 1.0 or 1.1 */
	if (sr_u16(r) != 0x4647) return false;
	GUIDE.visited = sr_u8(r);
	GUIDE.selected = sr_u8(r) % GUIDE_PLACES;
	int pin = sr_u8(r);
	GUIDE.pin = pin > 0 && pin <= GUIDE_PLACES ? pin - 1 : -1;
	sr_bytes(r, GUIDE.moments, GUIDE_MOMENTS);
	for (int i = 0; i < GUIDE_MOMENTS; i++) if (GUIDE.moments[i] > 11) GUIDE.moments[i] = 0;
	return !r->bad;
}
