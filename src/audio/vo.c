/*
 * vo.c -- the SD card voice-over loader (see vo.h).
 *
 * The card is mounted at boot (core/storage.c). One low-priority thread looks
 * for the vo folder and then serves one request at a time,
 * reading the whole line into one of two buffers. Two, because the mixer may
 * still be finishing one line while the next is read.
 */
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <sys/stat.h>
#include <gccore.h>
#include "audio/vo.h"
#include "core/storage.h"

#define VO_DIR "sd:/apps/lonelookout/vo"
#define VO_MAX_SECONDS 30
#define VO_MAX_SAMPLES (22050 * VO_MAX_SECONDS)
#define STACK_SIZE (16 * 1024)

static lwp_t s_thread;
static lwpq_t s_wake;
static u8 *s_stack;
static volatile bool s_ready_card, s_available;
static s16 *s_buf[2];
static int s_slot;

/* The request (game thread writes, loader reads) and its answer. */
static char s_req[48];
static volatile u32 s_req_seq, s_done_seq;
static volatile vo_status s_status;
static const s16 *volatile s_pcm;
static volatile int s_len;
static char s_last[48];               /* the last line asked for, for the dev overlay */

bool vo_available(void) { return s_available; }

static void load(const char *id)
{
	char path[96];
	snprintf(path, sizeof(path), VO_DIR "/%s.pcm", id);
	FILE *f = fopen(path, "rb");
	if (!f) {
		s_status = VO_MISSING;
		return;
	}
	s_slot ^= 1;
	s16 *dst = s_buf[s_slot];
	int n = (int)fread(dst, 2, VO_MAX_SAMPLES, f);
	fclose(f);
	/* The files are little-endian; the Wii is not. */
	for (int i = 0; i < n; i++) {
		u16 v = (u16)dst[i];
		dst[i] = (s16)((v >> 8) | (v << 8));
	}
	s_pcm = dst;
	s_len = n;
	s_status = n > 0 ? VO_READY : VO_MISSING;
}

static void *loader(void *arg)
{
	(void)arg;
	struct stat st;
	if (storage_ok() && stat(VO_DIR, &st) == 0 && S_ISDIR(st.st_mode)) {
		s_buf[0] = memalign(32, VO_MAX_SAMPLES * 2);
		s_buf[1] = memalign(32, VO_MAX_SAMPLES * 2);
		s_available = s_buf[0] && s_buf[1];
	}
	s_ready_card = true;
	for (;;) {
		u32 seq = s_req_seq;
		if (seq != s_done_seq) {
			char id[48];
			memcpy(id, s_req, sizeof(id));
			id[sizeof(id) - 1] = 0;
			if (s_available) load(id);
			else s_status = VO_MISSING;
			/* A newer request while loading: answer that one instead. */
			if (seq == s_req_seq) s_done_seq = seq;
			continue;
		}
		LWP_ThreadSleep(s_wake);
	}
	return NULL;
}

void vo_init(void)
{
	LWP_InitQueue(&s_wake);
	s_stack = memalign(32, STACK_SIZE);
	if (s_stack) LWP_CreateThread(&s_thread, loader, NULL, s_stack, STACK_SIZE, 40);
}

void vo_request(const char *line_id)
{
	if (!s_available) {
		s_status = VO_MISSING;
		return;
	}
	strncpy(s_req, line_id, sizeof(s_req) - 1);
	s_req[sizeof(s_req) - 1] = 0;
	memcpy(s_last, s_req, sizeof(s_last));
	s_status = VO_PENDING;
	s_req_seq++;
	LWP_ThreadSignal(s_wake);
}

vo_status vo_poll(const s16 **pcm, int *len)
{
	if (s_done_seq != s_req_seq) return VO_PENDING;      /* the latest request isn't answered yet */
	if (s_status == VO_READY) {
		*pcm = s_pcm;
		*len = s_len;
	}
	return s_status;
}

void vo_debug(char *out, int n)
{
	static const char *ST[] = { "none", "loading", "ready", "missing" };
	if (!s_ready_card) snprintf(out, n, "vo: mounting");
	else if (!s_available) snprintf(out, n, "vo: no vo folder");
	else snprintf(out, n, "vo: %s %s (%d samples)", s_last[0] ? s_last : "-", ST[s_status], s_status == VO_READY ? s_len : 0);
}
