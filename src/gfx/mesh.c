/*
 * mesh.c -- display-list helpers and flat-shaded box/beam builders used for
 * the tower blockout and other props.
 */
#include <malloc.h>
#include <math.h>
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

bool dl_rebegin(dlist_t *d, vdesc_fn vdesc)
{
	if (!d->buf) return false;
	d->len = 0;
	memset(d->buf, 0, d->cap);
	DCFlushRange(d->buf, d->cap);
	DCInvalidateRange(d->buf, d->cap);
	vdesc();
	GX_BeginDispList(d->buf, d->cap);
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

void mesh_box_yaw(v3 c, v3 h, float yaw, u32 col)
{
	/* Bearing convention: +z half axis points along the bearing (north = -z). */
	float s = sinf(yaw), co = cosf(yaw);
	v3 ax = v3_make(co * h.x, 0, s * h.x);       /* right of the bearing */
	v3 az = v3_make(-s * h.z, 0, co * h.z);      /* back along the bearing (keeps the axes right-handed) */
	box_axes(c, ax, v3_make(0, h.y, 0), az, col);
}

void mesh_quad(v3 a, v3 b, v3 c, v3 d, u32 col)
{
	v3 n = v3_norm(v3_cross(v3_sub(b, a), v3_sub(d, a)));
	GX_Begin(GX_QUADS, VF_3D, 4);
	quad(a, b, c, d, n, col);
	GX_End();
}

void mesh_prism(v3 a, v3 b, float r, int sides, u32 col, u32 col_end)
{
	v3 axis = v3_sub(b, a);
	float len = v3_len(axis);
	if (len < 1e-4f || sides < 3) return;
	v3 dir = v3_scale(axis, 1.0f / len);
	v3 ref = fabsf(dir.y) > 0.9f ? v3_make(1, 0, 0) : v3_make(0, 1, 0);
	v3 u = v3_norm(v3_cross(ref, dir));
	v3 w = v3_cross(dir, u);
	GX_Begin(GX_QUADS, VF_3D, sides * 4);
	for (int i = 0; i < sides; i++) {
		float a0 = LL_TAU * i / sides, a1 = LL_TAU * (i + 1) / sides;
		v3 o0 = v3_add(v3_scale(u, cosf(a0) * r), v3_scale(w, sinf(a0) * r));
		v3 o1 = v3_add(v3_scale(u, cosf(a1) * r), v3_scale(w, sinf(a1) * r));
		v3 n = v3_norm(v3_add(o0, o1));
		quad(v3_add(a, o0), v3_add(a, o1), v3_add(b, o1), v3_add(b, o0), n, col);
	}
	GX_End();
	/* End caps as fans. */
	for (int e = 0; e < 2; e++) {
		v3 c = e ? b : a;
		v3 n = e ? dir : v3_scale(dir, -1.0f);
		GX_Begin(GX_TRIANGLEFAN, VF_3D, sides + 2);
		emit_pnc(c, n, col_end);
		for (int k = 0; k <= sides; k++) {
			int i = e ? k : sides - k;
			float ang = LL_TAU * i / sides;
			emit_pnc(v3_add(c, v3_add(v3_scale(u, cosf(ang) * r), v3_scale(w, sinf(ang) * r))), n, col_end);
		}
		GX_End();
	}
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
