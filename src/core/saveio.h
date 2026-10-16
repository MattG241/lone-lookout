/*
 * saveio.h -- a tiny big-endian byte writer and reader, and CRC32, for the
 * save file. Pure C, so the host tests can round-trip saves.
 */
#ifndef LL_SAVEIO_H
#define LL_SAVEIO_H

#include <gctypes.h>
#include <stdbool.h>

typedef struct {
	u8 *buf;
	u32 cap, len;
	bool overflow;
} sw_t;

typedef struct {
	const u8 *buf;
	u32 len, pos;
	bool bad;            /* read past the end, or a malformed field */
} sr_t;

void sw_init(sw_t *w, u8 *buf, u32 cap);
void sw_u8(sw_t *w, u8 v);
void sw_u16(sw_t *w, u16 v);
void sw_u32(sw_t *w, u32 v);
void sw_f32(sw_t *w, float v);
void sw_str(sw_t *w, const char *s);              /* u16 length, then the bytes */
void sw_bytes(sw_t *w, const void *p, u32 n);

void sr_init(sr_t *r, const u8 *buf, u32 len);
u8 sr_u8(sr_t *r);
u16 sr_u16(sr_t *r);
u32 sr_u32(sr_t *r);
float sr_f32(sr_t *r);
int sr_str(sr_t *r, char *out, int cap);          /* truncates to cap-1; returns length read */
void sr_bytes(sr_t *r, void *p, u32 n);

u32 crc32_of(const void *data, u32 n);

#endif
