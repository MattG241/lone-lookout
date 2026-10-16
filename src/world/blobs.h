/*
 * blobs.h -- access to the baked world data.
 *
 * On the Wii the blobs are linked into the .dol (bin2o, see gen/assets.h);
 * the host-side tests load the same files from gen/ instead. Multi-byte
 * values in the blobs are big-endian (the Wii's byte order), so readers use
 * the be_* helpers and work on either host.
 */
#ifndef LL_BLOBS_H
#define LL_BLOBS_H

#include <gctypes.h>
#include <string.h>

typedef enum { BLOB_HEIGHT, BLOB_COLOR, BLOB_SURFACE, BLOB_TREES, BLOB_TUFTS, BLOB_COUNT } blob_id;

typedef struct {
	const u8 *data;
	u32 size;
} blob_t;

blob_t world_blob(blob_id id);

static inline u16 be_u16(const u8 *p) { return (u16)((p[0] << 8) | p[1]); }
static inline float be_f32(const u8 *p)
{
	u32 v = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
	float f;
	memcpy(&f, &v, 4);
	return f;
}

#endif
