/*
 * mesh.c -- display-list helpers and flat-shaded box/beam builders used for
 * the tower blockout and other props.
 */
#include <malloc.h>
#include <string.h>
#include "gfx/mesh.h"
#include "gfx/gfx.h"

bool dl_begin(dlist_t *d, u32 cap, vdesc_fn vdesc)
{
	cap = (cap + 63u) & ~31u;
	d->buf = memalign(32, cap);
	d->cap = cap;
	d->len = 0;
	if (!d->buf) return false;
	memset(d->buf, 0, cap);
	DCInvalidateRange(d->buf, cap);
	vdesc();                          /* commit state outside the list */
	GX_BeginDispList(d->buf, cap);
	return true;
}

bool dl_end(dlist_t *d)
{
	d->len = GX_EndDispList();
	return d->len > 0;
}

void dl_draw(const dlist_t *d)
{
	if (d->len)
		GX_CallDispList(d->buf, d->len);
}

static void quad(v3 a, v3 b, v3 c, v3 dd, v3 n, u32 col)
{
	emit_pnc(a, n, col);
	emit_pnc(b, n, col);
	emit_pnc(c, n, col);
	emit_pnc(dd, n, col);
}

/* Box from a centre, three (not necessarily axis-aligned) half axes. */
static void box_axes(v3 c, v3 ax, v3 ay, v3 az, u32 col)
{
	v3 p[8];
	for (int i = 0; i < 8; i++) {
		v3 o = c;
		o = v3_add(o, v3_scale(ax, (i & 1) ? 1.0f : -1.0f));
		o = v3_add(o, v3_scale(ay, (i & 2) ? 1.0f : -1.0f));
		o = v3_add(o, v3_scale(az, (i & 4) ? 1.0f : -1.0f));
		p[i] = o;
	}
	v3 nx = v3_norm(ax), ny = v3_norm(ay), nz = v3_norm(az);
	GX_Begin(GX_QUADS, VF_3D, 24);
	quad(p[1], p[3], p[7], p[5], nx, col);                 /* +x */
	quad(p[0], p[4], p[6], p[2], v3_scale(nx, -1), col);   /* -x */
	quad(p[2], p[6], p[7], p[3], ny, col);                 /* +y */
	quad(p[0], p[1], p[5], p[4], v3_scale(ny, -1), col);   /* -y */
	quad(p[4], p[5], p[7], p[6], nz, col);                 /* +z */
	quad(p[0], p[2], p[3], p[1], v3_scale(nz, -1), col);   /* -z */
	GX_End();
}

void mesh_box(v3 c, v3 h, u32 col)
{
	box_axes(c, v3_make(h.x, 0, 0), v3_make(0, h.y, 0), v3_make(0, 0, h.z), col);
}

void mesh_beam(v3 a, v3 b, float w, float t, u32 col)
{
	v3 axis = v3_sub(b, a);
	float len = v3_len(axis);
	if (len < 1e-4f) return;
	v3 dir = v3_scale(axis, 1.0f / len);
	v3 ref = fabsf(dir.y) > 0.9f ? v3_make(1, 0, 0) : v3_make(0, 1, 0);
	v3 side = v3_norm(v3_cross(ref, dir));
	v3 up = v3_norm(v3_cross(dir, side));
	v3 c = v3_scale(v3_add(a, b), 0.5f);
	box_axes(c, v3_scale(side, w * 0.5f), v3_scale(up, t * 0.5f), v3_scale(dir, len * 0.5f), col);
}

void mesh_tri(v3 a, v3 b, v3 c, u32 col)
{
	v3 n = v3_norm(v3_cross(v3_sub(b, a), v3_sub(c, a)));
	GX_Begin(GX_TRIANGLES, VF_3D, 3);
	emit_pnc(a, n, col);
	emit_pnc(b, n, col);
	emit_pnc(c, n, col);
	GX_End();
}
