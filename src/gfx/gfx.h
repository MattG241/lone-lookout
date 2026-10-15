/*
 * gfx.h -- video/GX setup, frame pacing, camera, materials and post passes.
 *
 * "Soft Paper Peaks" in hardware terms (see docs/DESIGN.md section 9):
 *   - two-tone light from a light-ramp texture indexed by N.L (normal texgen)
 *   - world-planar paper-tooth texture modulating every lit surface
 *   - hardware fog tinted by the colour script
 *   - EFB->XFB copy filter used as a gentle vertical softening
 *   - full-screen lift, warm tint, and grain + vignette passes
 */
#ifndef LL_GFX_H
#define LL_GFX_H

#include <gccore.h>
#include <stdbool.h>
#include "core/mathx.h"
#include "game/daycycle.h"

/* Vertex formats */
#define VF_3D GX_VTXFMT0     /* POS xyz f32, NRM xyz f32, CLR0 rgba8, TEX0 st f32 */
#define VF_2D GX_VTXFMT1     /* POS xy f32, CLR0 rgba8, TEX0 st f32 */

#define CAM_NEAR 0.08f
#define CAM_FAR 1700.0f

typedef struct {
	GXRModeObj *rmode;
	void *xfb[2];
	int fb;
	u16 efb_w, efb_h;
	bool widescreen;
	float aspect;            /* projection aspect (4:3 or anamorphic 16:9) */
	float ui_w, ui_h;        /* virtual UI canvas: 640x480, or 854x480 when widescreen */
	float refresh_hz;

	Mtx44 proj;
	Mtx view;
	v3 eye, fwd, right, up;
	float fovy_deg;

	int vsyncs;              /* 1 = full rate, 2 = half rate (30 fps lock) */
	bool force_half_rate;
	u32 frame;
	float work_ms;
} gfx_t;

extern gfx_t G;

/* Shared textures */
extern GXTexObj T_PAPER, T_GRAIN, T_GLOW, T_FOLIAGE, T_BACKDROP, T_FONT_S, T_FONT_L;

void gfx_init(void);
void gfx_begin(rgbf clear);
void gfx_end(void);

void gfx_camera(v3 eye, float yaw, float pitch, float roll, float fovy_deg);
void gfx_model(Mtx model);           /* world matrix for the next draws */
void gfx_model_identity(void);
void gfx_model_follow_eye(void);     /* translate to the eye: sky dome, backdrop */
void gfx_ortho(void);                /* UI canvas, origin top-left */
bool gfx_sphere_visible(v3 c, float r);

GXColor gx_col(rgbf c, float a);
void gfx_texobj(GXTexObj *o, const void *data, u16 w, u16 h, u8 fmt, u8 wrap, bool filter);

/* Vertex descriptors (must match how display lists were recorded) */
void vdesc_pnc(void);                /* POS NRM CLR0 */
void vdesc_pnct(void);               /* POS NRM CLR0 TEX0 */
void vdesc_pc(void);                 /* POS CLR0 (3D) */
void vdesc_pct(void);                /* POS CLR0 TEX0 (3D) */
void vdesc_2d(bool textured);        /* POS(xy) CLR0 [TEX0] */
/* Set and immediately commit the descriptor: use before recording or
 * calling display lists. */
void vdesc_pnc_flush(void);
void vdesc_pnct_flush(void);

/* Materials */
void ramp_update(const daylight *d);
void mat_lit_solid(void);            /* ramp x vertex colour x paper tooth */
void mat_lit_cutout(void);           /* ramp x vertex colour x foliage atlas, alpha-tested */
void mat_ramp_light(v3 light_model); /* key-light direction in the current model's space */
void mat_vcolor(void);               /* vertex colour only */
void mat_vcolor_tex(GXTexObj *t, bool tex_alpha_only, bool scale2);

void gfx_blend_none(void);
void gfx_blend_alpha(void);
void gfx_blend_add(void);
void gfx_depth(bool test, bool write);
void gfx_fog(const daylight *d);
void gfx_fog_off(void);

/* Full-screen grade, drawn after the 3D scene and before the UI. */
void gfx_post(const daylight *d, float fade_to_black);

/* 2D helpers on the UI canvas */
void ui_rect(float x, float y, float w, float h, GXColor c);
void ui_rect_grad(float x, float y, float w, float h, GXColor top, GXColor bottom);

#endif
