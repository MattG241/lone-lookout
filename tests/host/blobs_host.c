/*
 * blobs_host.c -- host-side stand-in for src/world/blobs.c: reads the baked
 * world blobs from gen/ instead of the .dol.
 */
#include <stdio.h>
#include <stdlib.h>
#include "world/blobs.h"

#ifndef GEN_DIR
#define GEN_DIR "gen"
#endif

static blob_t s_blobs[BLOB_COUNT];

static blob_t load(const char *name)
{
	char path[256];
	snprintf(path, sizeof(path), "%s/%s.bin", GEN_DIR, name);
	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "cannot open %s (run make first)\n", path);
		exit(2);
	}
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	u8 *buf = malloc(n);
	if (fread(buf, 1, n, f) != (size_t)n) exit(2);
	fclose(f);
	return (blob_t){ buf, (u32)n };
}

blob_t world_blob(blob_id id)
{
	static const char *names[BLOB_COUNT] = { "world_height", "world_color", "world_surface", "world_trees", "world_tufts" };
	if (!s_blobs[id].data) s_blobs[id] = load(names[id]);
	return s_blobs[id];
}
