/*
 * saveio.c -- see saveio.h.
 */
#include <string.h>
#include "core/saveio.h"

void sw_init(sw_t *w, u8 *buf, u32 cap)
{
	w->buf = buf;
	w->cap = cap;
	w->len = 0;
	w->overflow = false;
}

void sw_bytes(sw_t *w, const void *p, u32 n)
{
	if (w->len + n > w->cap) {
		w->overflow = true;
		return;
	}
	memcpy(w->buf + w->len, p, n);
	w->len += n;
}

void sw_u8(sw_t *w, u8 v) { sw_bytes(w, &v, 1); }

void sw_u16(sw_t *w, u16 v)
{
	u8 b[2] = { (u8)(v >> 8), (u8)v };
	sw_bytes(w, b, 2);
}

void sw_u32(sw_t *w, u32 v)
{
	u8 b[4] = { (u8)(v >> 24), (u8)(v >> 16), (u8)(v >> 8), (u8)v };
	sw_bytes(w, b, 4);
}

void sw_f32(sw_t *w, float v)
{
	u32 u;
	memcpy(&u, &v, 4);
	sw_u32(w, u);
}

void sw_str(sw_t *w, const char *s)
{
	u32 n = s ? (u32)strlen(s) : 0;
	if (n > 0xFFFF) n = 0xFFFF;
	sw_u16(w, (u16)n);
	if (n) sw_bytes(w, s, n);
}

void sr_init(sr_t *r, const u8 *buf, u32 len)
{
	r->buf = buf;
	r->len = len;
	r->pos = 0;
	r->bad = false;
}

void sr_bytes(sr_t *r, void *p, u32 n)
{
	if (r->bad || r->pos + n > r->len) {
		r->bad = true;
		memset(p, 0, n);
		return;
	}
	memcpy(p, r->buf + r->pos, n);
	r->pos += n;
}

u8 sr_u8(sr_t *r)
{
	u8 v;
	sr_bytes(r, &v, 1);
	return v;
}

u16 sr_u16(sr_t *r)
{
	u8 b[2];
	sr_bytes(r, b, 2);
	return (u16)((b[0] << 8) | b[1]);
}

u32 sr_u32(sr_t *r)
{
	u8 b[4];
	sr_bytes(r, b, 4);
	return ((u32)b[0] << 24) | ((u32)b[1] << 16) | ((u32)b[2] << 8) | b[3];
}

float sr_f32(sr_t *r)
{
	u32 u = sr_u32(r);
	float v;
	memcpy(&v, &u, 4);
	return v == v ? v : 0.0f;          /* never hand back a NaN */
}

int sr_str(sr_t *r, char *out, int cap)
{
	u16 n = sr_u16(r);
	if (r->bad || r->pos + n > r->len) {
		r->bad = true;
		if (cap > 0) out[0] = 0;
		return 0;
	}
	int keep = n < cap - 1 ? n : cap - 1;
	if (keep > 0) memcpy(out, r->buf + r->pos, keep);
	if (cap > 0) out[keep > 0 ? keep : 0] = 0;
	r->pos += n;
	return n;
}

u32 crc32_of(const void *data, u32 n)
{
	static u32 table[256];
	static bool ready;
	if (!ready) {
		for (u32 i = 0; i < 256; i++) {
			u32 c = i;
			for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
		ready = true;
	}
	const u8 *p = data;
	u32 c = 0xFFFFFFFFu;
	for (u32 i = 0; i < n; i++) c = table[(c ^ p[i]) & 255] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}
