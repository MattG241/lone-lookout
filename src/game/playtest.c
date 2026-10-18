/*
 * playtest.c -- the playtest log (see playtest.h).
 *
 * Lines collect in a small buffer and go to the card in one append when it
 * fills, every five minutes, on each new day, and on save, pause and quit, so
 * the card is written rarely and a crash loses at most a few minutes.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ogc/lwp_watchdog.h>
#include "game/playtest.h"
#include "game/game.h"
#include "game/radio.h"
#include "core/storage.h"
#include "input/input.h"
#include "world/world.h"

#define LOG_PATH STORAGE_DIR "/playtest.log"
#define BUF_SIZE 8192
#define IDLE_AFTER 30.0f              /* seconds without input that count as standing still */

static char s_buf[BUF_SIZE];
static int s_len;
static u64 s_t0;
static float s_since_flush, s_since_where, s_idle;
static bool s_idle_logged, s_started;

bool playtest_on(void) { return OPT.playtest && storage_ok(); }

static float now_s(void)
{
	if (!s_t0) s_t0 = gettime();
	return ticks_to_millisecs(gettime() - s_t0) * 0.001f;
}

void playtest_flush(void)
{
	if (!s_len) return;
	if (storage_ok()) {
		FILE *f = fopen(LOG_PATH, "a");
		if (f) {
			fwrite(s_buf, 1, s_len, f);
			fclose(f);
		}
	}
	s_len = 0;
	s_since_flush = 0.0f;
}

void playtest_event(const char *event, const char *fmt, ...)
{
	if (!playtest_on()) return;
	if (!s_started) {
		s_started = true;
		/* A header line per session, so logs from several sittings can share a file. */
		char head[160];
		snprintf(head, sizeof(head), "#session\t%s\t%s\n", "Lone Lookout 1.2", IN.nunchuk ? "nunchuk" : "remote only");
		int n = (int)strlen(head);
		if (s_len + n < BUF_SIZE) {
			memcpy(s_buf + s_len, head, n);
			s_len += n;
		}
	}
	char line[256];
	int h = (int)DAY.hour % 24, m = (int)((DAY.hour - floorf(DAY.hour)) * 60.0f);
	int n = snprintf(line, sizeof(line), "%.1f\t%d\t%02d:%02d\t%s", now_s(), S.day, h, m, event);
	if (fmt && n < (int)sizeof(line) - 2) {
		line[n++] = '\t';
		va_list ap;
		va_start(ap, fmt);
		n += vsnprintf(line + n, sizeof(line) - n, fmt, ap);
		va_end(ap);
	}
	if (n > (int)sizeof(line) - 2) n = (int)sizeof(line) - 2;
	line[n++] = '\n';
	if (s_len + n >= BUF_SIZE) playtest_flush();
	memcpy(s_buf + s_len, line, n);
	s_len += n;
}

void playtest_update(float dt)
{
	if (!playtest_on()) return;
	s_since_flush += dt;
	s_since_where += dt;
	if (s_since_where >= 60.0f) {
		s_since_where = 0.0f;
		playtest_event("where", "%.0f %.0f\tarea %d\tmode %d", P.pos.x, P.pos.z, S.area, (int)P.mode);
	}
	/* Standing still: no buttons, no stick, the pointer not moving, and nothing on the radio. */
	static float last_x, last_y;
	bool moved = IN.down || IN.held || fabsf(IN.stick_x) > 0.2f || fabsf(IN.stick_y) > 0.2f ||
	             fabsf(IN.ir_x - last_x) + fabsf(IN.ir_y - last_y) > 0.02f;
	last_x = IN.ir_x;
	last_y = IN.ir_y;
	if (moved || radio_busy()) {
		if (s_idle_logged) playtest_event("idle_end", "%.0f", s_idle);
		s_idle = 0.0f;
		s_idle_logged = false;
	} else {
		s_idle += dt;
		if (!s_idle_logged && s_idle >= IDLE_AFTER) {
			s_idle_logged = true;
			playtest_event("idle", "%.0f %.0f\tarea %d\tmode %d", P.pos.x, P.pos.z, S.area, (int)P.mode);
		}
	}
	if (s_since_flush >= 300.0f) playtest_flush();
}
