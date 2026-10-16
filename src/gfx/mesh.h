/*
 * mesh.h -- display-list recording and simple solid primitives.
 *
 * Static geometry is recorded once into GX display lists and replayed every
 * frame. The vertex descriptor is committed (vdesc_*_flush) before a list
 * is recorded and again before it is replayed, so lists contain only
 * primitives and never depend on whatever the previous draw left behind.
 */
#ifndef LL_MESH_H
#define LL_MESH_H

#include <gccore.h>
#include <stdbool.h>
#include "core/mathx.h"

typedef struct {
	void *buf;
	u32 cap;
	u32 len;
	u16 stride;                      /* bytes per vertex */
	u32 tris;                        /* triangles in the list */
} dlist_t;

typedef void (*vdesc_fn)(void);
bool dl_begin(dlist_t *d, u32 cap_bytes, vdesc_fn vdesc);
/* Record again into the list's existing buffer (same or smaller contents).
 * The GPU must be idle (GX_DrawDone) -- used for the seasonal recolour. */
bool dl_rebegin(dlist_t *d, vdesc_fn vdesc);
bool dl_end(dlist_t *d);             /* false if the list overflowed */
void dl_draw(const dlist_t *d);

static inline u32 rgba_u32(rgbf c, float a)
{
	u32 r = (u32)(saturatef(c.r) * 255.0f + 0.5f), g = (u32)(saturatef(c.g) * 255.0f + 0.5f);
	u32 b = (u32)(saturatef(c.b) * 255.0f + 0.5f), al = (u32)(saturatef(a) * 255.0f + 0.5f);
	return (r << 24) | (g << 16) | (b << 8) | al;
}

static inline void emit_pnc(v3 p, v3 n, u32 rgba)
{
	GX_Position3f32(p.x, p.y, p.z);
	GX_Normal3f32(n.x, n.y, n.z);
	GX_Color1u32(rgba);
}

static inline void emit_pnct(v3 p, v3 n, u32 rgba, float s, float t)
{
	GX_Position3f32(p.x, p.y, p.z);
	GX_Normal3f32(n.x, n.y, n.z);
	GX_Color1u32(rgba);
	GX_TexCoord2f32(s, t);
}

/* Solid primitives for POS NRM CLR0 lists. Each emits its own GX_Begin.
 * Faces wind counter-clockwise seen from outside. */
#define MESH_BOX_BYTES (3 + 24 * 28)
void mesh_box(v3 c, v3 half, u32 col);
void mesh_box_yaw(v3 c, v3 half, float yaw, u32 col);  /* rotated about y (bearing) */
void mesh_beam(v3 a, v3 b, float w, float t, u32 col);  /* w across, t "up" */
void mesh_tri(v3 a, v3 b, v3 c, u32 col);               /* flat-shaded triangle */
void mesh_quad(v3 a, v3 b, v3 c, v3 d, u32 col);        /* flat-shaded quad */
/* An n-sided prism (log, post, pipe) from a to b; caps in col_end. */
void mesh_prism(v3 a, v3 b, float r, int sides, u32 col, u32 col_end);

#endif
