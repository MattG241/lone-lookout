/*
 * test_audio.c -- host-side checks of the music engine (src/audio/music.c).
 *
 * Renders every piece start to finish, as the mixer would, in 512-sample
 * blocks: no NaNs, no runaway levels, nothing silent, the right length. Each
 * instrument is checked for tuning (autocorrelation pitch of a held note),
 * and the render is timed to estimate the cost on the Wii. The sound log's
 * beds (src/audio/ambient.c) get the same level and cost checks.
 *
 * Set LL_WAV_DIR to also write each piece as a 16-bit WAV for listening.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "audio/music.h"
#include "audio/ambient.h"
#include "audio/dsp.h"

static int s_fails, s_checks;

#define CHECK(cond, ...) do { \
	s_checks++; \
	if (!(cond)) { s_fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

#define BLOCK 512
#define MAX_SECONDS 200

static float s_l[(int)SR * MAX_SECONDS], s_r[(int)SR * MAX_SECONDS];

static void write_wav(const char *path, int n)
{
	FILE *f = fopen(path, "wb");
	if (!f) return;
	unsigned bytes = (unsigned)n * 4;
	unsigned hdr[11] = { 0x46464952, 36 + bytes, 0x45564157, 0x20746d66, 16, 0x00020001, (unsigned)SR,
	                     (unsigned)SR * 4, 0x00100004, 0x61746164, bytes };
	fwrite(hdr, 4, 11, f);
	for (int i = 0; i < n; i++) {
		short s[2] = { (short)(soft_clip(s_l[i] * 1.4f) * 0.9f * 32000.0f), (short)(soft_clip(s_r[i] * 1.4f) * 0.9f * 32000.0f) };
		fwrite(s, 2, 2, f);
	}
	fclose(f);
}

/* Render until the music has finished ringing out; returns samples. */
static int render_all(double *cpu_s)
{
	int n = 0;
	clock_t t0 = clock();
	memset(s_l, 0, sizeof(s_l));
	memset(s_r, 0, sizeof(s_r));
	while (n + BLOCK <= (int)SR * MAX_SECONDS) {
		music_render(s_l + n, s_r + n, BLOCK, 1.0f);
		n += BLOCK;
		if (!music_active()) break;
	}
	*cpu_s = (double)(clock() - t0) / CLOCKS_PER_SEC;
	return n;
}

static void test_pieces(void)
{
	const char *dir = getenv("LL_WAV_DIR");
	double cpu_total = 0.0, audio_total = 0.0;
	CHECK(music_count() == 6, "%d pieces, expected 6", music_count());
	for (int p = 0; p < music_count(); p++) {
		for (int resolve = 0; resolve <= (p == 0); resolve++) {
			music_init(1234u + p);
			music_play(p, resolve);
			double cpu;
			int n = render_all(&cpu);
			float peak = 0.0f;
			double sum = 0.0;
			bool finite = true;
			for (int i = 0; i < n; i++) {
				if (!isfinite(s_l[i]) || !isfinite(s_r[i])) finite = false;
				peak = fmaxf(peak, fmaxf(fabsf(s_l[i]), fabsf(s_r[i])));
				sum += s_l[i] * s_l[i] + s_r[i] * s_r[i];
			}
			float rms = (float)sqrt(sum / (2.0 * n)), secs = n / SR;
			printf("  %-16s %s %5.1f s  peak %.2f  rms %.3f  %.1f ms per second of music\n", music_title(p),
			       resolve ? "(coda)" : "      ", secs, peak, rms, cpu / secs * 1000.0);
			CHECK(finite, "%s: NaN or infinity", music_title(p));
			CHECK(peak < 1.0f, "%s: peak %.2f", music_title(p), peak);
			CHECK(rms > 0.015f && rms < 0.2f, "%s: rms %.3f", music_title(p), rms);
			CHECK(secs > 45.0f && secs < 150.0f, "%s: %.1f s long", music_title(p), secs);
			cpu_total += cpu;
			audio_total += secs;
			if (dir) {
				char path[512];
				snprintf(path, sizeof(path), "%s/music_%d%s.wav", dir, p + 1, resolve ? "_coda" : "");
				write_wav(path, n);
			}
		}
	}
	/* A 3 GHz desktop core does very roughly 15-25x the Wii's Broadway (729 MHz, no SIMD). */
	double per_s = cpu_total / audio_total;
	printf("  music costs %.2f ms per second here; ~%.0f%% of a Wii core at a 20x ratio\n", per_s * 1000.0,
	       per_s * 20.0 * 100.0);
	CHECK(per_s * 20.0 < 0.15, "music would take %.0f%% of the Wii's CPU", per_s * 20.0 * 100.0);
}

/* Pitch by autocorrelation over a window, refined with a parabola. */
static float pitch_of(const float *x, int n, float expect)
{
	int lo = (int)(SR / (expect * 1.08f)), hi = (int)(SR / (expect * 0.92f)) + 1;
	double best = -1e30;
	int bl = lo;
	double r[2048];
	for (int lag = lo - 1; lag <= hi + 1; lag++) {
		double acc = 0.0;
		for (int i = 0; i + lag < n; i++) acc += x[i] * x[i + lag];
		r[lag - lo + 1] = acc;
		if (lag >= lo && lag <= hi && acc > best) {
			best = acc;
			bl = lag;
		}
	}
	double a = r[bl - lo], b = r[bl - lo + 1], c = r[bl - lo + 2];
	double off = (a - c) / (2.0 * (a - 2.0 * b + c));
	return (float)(SR / (bl + off));
}

static void test_tuning(void)
{
	static const struct { int voice, note; const char *name; } T[] = {
		{ MV_GUITAR, 38, "guitar D2" }, { MV_GUITAR, 57, "guitar A3" }, { MV_GUITAR, 74, "guitar D5" },
		{ MV_PIANO, 50, "piano D3" }, { MV_PIANO, 69, "piano A4" }, { MV_PIANO, 81, "piano A5" },
		{ MV_HARMONIC, 74, "harmonic D5" }, { MV_DRONE, 38, "drone D2" },
	};
	for (unsigned k = 0; k < sizeof(T) / sizeof(T[0]); k++) {
		music_init(7u);
		music_debug_note(T[k].voice, T[k].note, 0.7f, 3.0f);
		memset(s_l, 0, sizeof(float) * (int)SR * 3);
		memset(s_r, 0, sizeof(float) * (int)SR * 3);
		for (int n = 0; n < (int)SR * 3; n += BLOCK) music_render(s_l + n, s_r + n, BLOCK, 1.0f);
		/* A window after the attack (drones swell slowly). */
		int at = T[k].voice == MV_DRONE ? (int)SR * 2 : (int)(SR * 0.4f);
		static float mono[8192];
		for (int i = 0; i < 8192; i++) mono[i] = s_l[at + i] + s_r[at + i];
		float want = 440.0f * powf(2.0f, (T[k].note - 69) / 12.0f);
		float got = pitch_of(mono, 8192, want);
		float cents = 1200.0f * log2f(got / want);
		CHECK(fabsf(cents) < 10.0f, "%s: %.1f Hz, wanted %.1f (%+.1f cents)", T[k].name, got, want, cents);
	}
	printf("  tuning: guitar, piano, harmonics and drone within 10 cents\n");
}

static void test_beds(void)
{
	static const char *NAMES[SND_COUNT] = { "stair", "creek", "grasshoppers", "elk", "sandpiper", "frogs", "pika",
	                                        "snags", "woodpecker", "chimes", "tin_rain", "nothing" };
	const char *dir = getenv("LL_WAV_DIR");
	const int secs = 12, n = (int)SR * secs;
	double cpu_all = 0.0;
	ambient_init();
	for (int k = 0; k < SND_COUNT; k++) {
		if (k == SND_STAIR || k == SND_ELK) continue;       /* one-shots, played as UI sounds */
		float gain[SND_COUNT] = { 0 }, pan[SND_COUNT] = { 0 };
		gain[k] = 1.0f;
		memset(s_l, 0, sizeof(float) * n);
		memset(s_r, 0, sizeof(float) * n);
		clock_t t0 = clock();
		for (int i = 0; i + BLOCK <= n; i += BLOCK) ambient_render(s_l + i, s_r + i, BLOCK, gain, pan, 0.8f);
		double cpu = (double)(clock() - t0) / CLOCKS_PER_SEC;
		cpu_all += cpu;
		float peak = 0.0f;
		double sum = 0.0;
		bool finite = true;
		for (int i = (int)SR; i < n; i++) {                  /* after the gain has eased in */
			if (!isfinite(s_l[i])) finite = false;
			peak = fmaxf(peak, fabsf(s_l[i]));
			sum += s_l[i] * s_l[i];
		}
		float rms = (float)sqrt(sum / (n - SR));
		CHECK(finite, "%s: NaN", NAMES[k]);
		CHECK(peak < 0.9f, "%s: peak %.2f", NAMES[k], peak);
		CHECK(rms > 0.001f && rms < 0.2f, "%s: rms %.4f", NAMES[k], rms);
		printf("  bed %-13s peak %.2f  rms %.3f  %.2f ms per second\n", NAMES[k], peak, rms, cpu / secs * 1000.0);
		if (dir) {
			char path[512];
			snprintf(path, sizeof(path), "%s/sound_%02d_%s.wav", dir, k + 1, NAMES[k]);
			write_wav(path, n);
		}
	}
	printf("  all ten beds at once would cost ~%.0f%% of a Wii core at a 20x ratio\n", cpu_all / secs * 20.0 * 100.0);
}

int main(void)
{
	printf("Audio tests\n");
	test_tuning();
	test_pieces();
	test_beds();
	printf("%d checks, %d failed\n", s_checks, s_fails);
	return s_fails ? 1 : 0;
}
