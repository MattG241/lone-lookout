/*
 * gfx.c -- video and GX initialisation, frame loop, camera and materials.
 */
#include <malloc.h>
#include <string.h>
#include <ogc/lwp_watchdog.h>
#include "gfx/gfx.h"
#include "core/noise.h"
#include "assets.h"

#define FIFO_SIZE (256 * 1024)
/* Meshes are wound counter-clockwise seen from outside (right-handed, the
 * OpenGL habit). GX's "front" is the opposite winding, so culling GX's front
 * faces is what removes our back faces. */
#define GFX_SOLID_CULL GX_CULL_FRONT

gfx_t G;
GXTexObj T_PAPER, T_GRAIN, T_GLOW, T_FOLIAGE, T_BACKDROP, T_FONT_S, T_FONT_L;

static void *s_fifo;
static GXTexObj s_ramp_obj[2];
static u8 *s_ramp_buf[2];
static int s_ramp_cur;
static u64 s_frame_start;
#ifndef LL_SHOT_TOUR
static int s_slow_frames, s_window, s_fast_streak;
#endif
static bool s_first_frame = true;

/* Copy filter: the hardware deflicker filter, reused as gentle softening.
 * Seven taps over lines (-1,-1, 0,0,0, +1,+1), summing to 64. */
static u8 s_soft_vfilter[7] = { 6, 6, 10, 20, 10, 6, 6 };

GXColor gx_col(rgbf c, float a)
{
	GXColor g = { (u8)(saturatef(c.r) * 255.0f + 0.5f), (u8)(saturatef(c.g) * 255.0f + 0.5f),
	              (u8)(saturatef(c.b) * 255.0f + 0.5f), (u8)(saturatef(a) * 255.0f + 0.5f) };
	return g;
}

void gfx_texobj(GXTexObj *o, const void *data, u16 w, u16 h, u8 fmt, u8 wrap, bool filter)
{
	GX_InitTexObj(o, (void *)data, w, h, fmt, wrap, wrap, GX_FALSE);
	u8 f = filter ? GX_LINEAR : GX_NEAR;
	GX_InitTexObjLOD(o, f, f, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
}

static void init_textures(void)
{
	/* The DOL loader has written these through the data cache; make sure
	 * the GPU (which reads main memory directly) sees the bytes. */
	DCFlushRange((void *)tex_paper_bin, tex_paper_bin_size);
	DCFlushRange((void *)tex_grain_bin, tex_grain_bin_size);
	DCFlushRange((void *)tex_glow_bin, tex_glow_bin_size);
	DCFlushRange((void *)tex_foliage_bin, tex_foliage_bin_size);
	DCFlushRange((void *)tex_backdrop_bin, tex_backdrop_bin_size);
	DCFlushRange((void *)font_small_bin, font_small_bin_size);
	DCFlushRange((void *)font_large_bin, font_large_bin_size);

	gfx_texobj(&T_PAPER, tex_paper_bin, TEX_PAPER_W, TEX_PAPER_H, TEX_PAPER_FMT, GX_REPEAT, true);
	gfx_texobj(&T_GRAIN, tex_grain_bin, TEX_GRAIN_W, TEX_GRAIN_H, TEX_GRAIN_FMT, GX_REPEAT, false);
	gfx_texobj(&T_GLOW, tex_glow_bin, TEX_GLOW_W, TEX_GLOW_H, TEX_GLOW_FMT, GX_CLAMP, true);
	gfx_texobj(&T_FOLIAGE, tex_foliage_bin, TEX_FOLIAGE_W, TEX_FOLIAGE_H, TEX_FOLIAGE_FMT, GX_CLAMP, true);
	gfx_texobj(&T_BACKDROP, tex_backdrop_bin, TEX_BACKDROP_W, TEX_BACKDROP_H, TEX_BACKDROP_FMT, GX_REPEAT, true);
	gfx_texobj(&T_FONT_S, font_small_bin, FONT_SMALL_W, FONT_SMALL_H, FONT_SMALL_FMT, GX_CLAMP, true);
	gfx_texobj(&T_FONT_L, font_large_bin, FONT_LARGE_W, FONT_LARGE_H, FONT_LARGE_FMT, GX_CLAMP, true);

	/* Light ramp: 64x4 RGBA8, double-buffered so we never rewrite the one
	 * the GPU may still be sampling. */
	for (int i = 0; i < 2; i++) {
		s_ramp_buf[i] = memalign(32, 64 * 4 * 4);
		memset(s_ramp_buf[i], 0x80, 64 * 4 * 4);
		DCFlushRange(s_ramp_buf[i], 64 * 4 * 4);
		gfx_texobj(&s_ramp_obj[i], s_ramp_buf[i], 64, 4, GX_TF_RGBA8, GX_CLAMP, true);
	}
}

void gfx_init(void)
{
	memset(&G, 0, sizeof(G));
	VIDEO_Init();
	G.rmode = VIDEO_GetPreferredMode(NULL);
	G.widescreen = CONF_GetAspectRatio() == CONF_ASPECT_16_9;
	G.aspect = G.widescreen ? 16.0f / 9.0f : 4.0f / 3.0f;
	G.ui_h = 480.0f;
	G.ui_w = G.widescreen ? 854.0f : 640.0f;
	G.refresh_hz = (G.rmode->viTVMode >> 2) == VI_PAL ? 50.0f : 60.0f;

	for (int i = 0; i < 2; i++)
		G.xfb[i] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(G.rmode));
	VIDEO_Configure(G.rmode);
	VIDEO_SetNextFramebuffer(G.xfb[0]);
	VIDEO_SetBlack(TRUE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (G.rmode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();

	s_fifo = memalign(32, FIFO_SIZE);
	memset(s_fifo, 0, FIFO_SIZE);
	GX_Init(s_fifo, FIFO_SIZE);

	G.efb_w = G.rmode->fbWidth;
	G.efb_h = G.rmode->efbHeight;
	GX_SetCopyClear((GXColor){ 0, 0, 0, 255 }, GX_MAX_Z24);
	GX_SetViewport(0, 0, G.efb_w, G.efb_h, 0, 1);
	f32 yscale = GX_GetYScaleFactor(G.rmode->efbHeight, G.rmode->xfbHeight);
	u32 xfb_h = GX_SetDispCopyYScale(yscale);
	GX_SetScissor(0, 0, G.efb_w, G.efb_h);
	GX_SetDispCopySrc(0, 0, G.efb_w, G.efb_h);
	GX_SetDispCopyDst(G.efb_w, xfb_h);
	GX_SetCopyFilter(G.rmode->aa, G.rmode->sample_pattern, GX_TRUE, s_soft_vfilter);
	GX_SetFieldMode(G.rmode->field_rendering,
	                ((G.rmode->viHeight == 2 * G.rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));
	GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	GX_SetDispCopyGamma(GX_GM_1_0);
	GX_SetCullMode(GX_CULL_BACK);
	GX_SetDither(GX_ENABLE);
	GX_CopyDisp(G.xfb[0], GX_TRUE);

	GX_SetVtxAttrFmt(VF_3D, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(VF_3D, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(VF_3D, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(VF_3D, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	GX_SetVtxAttrFmt(VF_2D, GX_VA_POS, GX_POS_XY, GX_F32, 0);
	GX_SetVtxAttrFmt(VF_2D, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(VF_2D, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

	init_textures();
	G.vsyncs = 1;
#ifdef LL_SHOT_TOUR
	G.force_half_rate = false;
#endif
}

/* ------------------------------------------------------------------------ */
/* Frame                                                                    */
/* ------------------------------------------------------------------------ */

void gfx_begin(rgbf clear)
{
	s_frame_start = gettime();
	GX_SetCopyClear(gx_col(clear, 1.0f), GX_MAX_Z24);
	GX_SetViewport(0, 0, G.efb_w, G.efb_h, 0, 1);
	GX_InvVtxCache();
	GX_InvalidateTexAll();
}

static void update_pacing(void)
{
#ifndef LL_SHOT_TOUR
	float budget = 1000.0f / G.refresh_hz * 0.93f;
	s_window++;
	if (G.work_ms > budget) s_slow_frames++;
	if (G.vsyncs == 1 && s_window >= 60) {
		if (s_slow_frames >= 8) {
			G.vsyncs = 2;           /* drop to an even half rate: no judder */
			s_fast_streak = 0;
		}
		s_window = s_slow_frames = 0;
	} else if (G.vsyncs == 2) {
		s_fast_streak = (G.work_ms < budget * 0.7f) ? s_fast_streak + 1 : 0;
		if (s_fast_streak > (int)(G.refresh_hz * 5.0f) / 2 && !G.force_half_rate) {
			G.vsyncs = 1;
			s_window = s_slow_frames = 0;
		}
	}
	if (G.force_half_rate) G.vsyncs = 2;
#endif
}

void gfx_end(void)
{
	GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
	GX_SetColorUpdate(GX_TRUE);
	GX_CopyDisp(G.xfb[G.fb], GX_TRUE);
	GX_DrawDone();
	G.work_ms = ticks_to_microsecs(gettime() - s_frame_start) * 0.001f;
	update_pacing();

	VIDEO_SetNextFramebuffer(G.xfb[G.fb]);
	if (s_first_frame) {
		VIDEO_SetBlack(FALSE);
		s_first_frame = false;
	}
	VIDEO_Flush();
	for (int i = 0; i < G.vsyncs; i++)
		VIDEO_WaitVSync();
	G.fb ^= 1;
	G.frame++;
}

/* ------------------------------------------------------------------------ */
/* Camera and matrices                                                      */
/* ------------------------------------------------------------------------ */

void gfx_camera(v3 eye, float yaw, float pitch, float roll, float fovy_deg)
{
	G.eye = eye;
	G.fovy_deg = fovy_deg;
	/* yaw 0 looks north (-z); positive yaw turns right (toward east). */
	G.fwd = v3_make(sinf(yaw) * cosf(pitch), sinf(pitch), -cosf(yaw) * cosf(pitch));
	v3 right = v3_make(cosf(yaw), 0.0f, sinf(yaw));
	v3 up = v3_cross(right, G.fwd);
	/* Head roll: rotate right/up around the view direction. */
	float cr = cosf(roll), sr = sinf(roll);
	G.right = v3_add(v3_scale(right, cr), v3_scale(up, sr));
	G.up = v3_sub(v3_scale(up, cr), v3_scale(right, sr));

	guVector p = { eye.x, eye.y, eye.z };
	guVector t = { eye.x + G.fwd.x, eye.y + G.fwd.y, eye.z + G.fwd.z };
	guVector u = { G.up.x, G.up.y, G.up.z };
	guLookAt(G.view, &p, &u, &t);
	guPerspective(G.proj, fovy_deg, G.aspect, CAM_NEAR, CAM_FAR);
	GX_LoadProjectionMtx(G.proj, GX_PERSPECTIVE);
}

/*
 * Matrix maths uses libogc's portable C routines (c_guMtx*). With the
 * paired-single versions, one build variant drew the terrain with a stale
 * model matrix (the world appeared upside down) while another identical-logic
 * build did not -- a layout-dependent fault we did not chase further. A few
 * hundred matrices a frame cost nothing in C.
 */
void gfx_model(Mtx model)
{
	Mtx mv, nrm;
	c_guMtxConcat(G.view, model, mv);
	GX_LoadPosMtxImm(mv, GX_PNMTX0);
	c_guMtxInverse(mv, nrm);
	c_guMtxTranspose(nrm, nrm);
	GX_LoadNrmMtxImm(nrm, GX_PNMTX0);
	GX_SetCurrentMtx(GX_PNMTX0);
}

void gfx_model_identity(void)
{
	Mtx m;
	c_guMtxIdentity(m);
	gfx_model(m);
}

void gfx_model_follow_eye(void)
{
	Mtx m;
	c_guMtxTrans(m, G.eye.x, G.eye.y, G.eye.z);
	gfx_model(m);
}

void gfx_ortho(void)
{
	Mtx44 o;
	Mtx m;
	guOrtho(o, 0.0f, G.ui_h, 0.0f, G.ui_w, 0.0f, 1.0f);
	GX_LoadProjectionMtx(o, GX_ORTHOGRAPHIC);
	c_guMtxIdentity(m);
	GX_LoadPosMtxImm(m, GX_PNMTX0);
	GX_SetCurrentMtx(GX_PNMTX0);
}

bool gfx_sphere_visible(v3 c, float r)
{
	v3 d = v3_sub(c, G.eye);
	float z = v3_dot(d, G.fwd);
	if (z < -r) return false;
	if (z - r > CAM_FAR) return false;
	float th = tanf(DEG2RAD(G.fovy_deg) * 0.5f);
	float tw = th * G.aspect;
	float x = fabsf(v3_dot(d, G.right));
	float y = fabsf(v3_dot(d, G.up));
	/* Conservative cone test, generous by the sphere radius. */
	float zc = z > 0.0f ? z : 0.0f;
	return x <= zc * tw + r * 1.5f && y <= zc * th + r * 1.5f;
}

/* ------------------------------------------------------------------------ */
/* Vertex descriptors                                                       */
/* ------------------------------------------------------------------------ */

void vdesc_pnc(void)
{
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_NRM, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
}

void vdesc_pnct(void)
{
	vdesc_pnc();
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
}

void vdesc_pc(void)
{
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
}

void vdesc_pct(void)
{
	vdesc_pc();
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
}

/*
 * libogc defers vertex-descriptor changes until the next GX_Begin, and a
 * GX_CallDispList does not count. Before recording or replaying a display
 * list we therefore "prime" the descriptor with a zero-area triangle (it
 * rasterises nothing), so the GPU decodes the list with the right layout and
 * no register writes ever end up inside a list.
 */
static void prime_triangle(bool tex)
{
	GX_Begin(GX_TRIANGLES, VF_3D, 3);
	for (int i = 0; i < 3; i++) {
		GX_Position3f32(0.0f, -1.0e4f, 0.0f);
		GX_Normal3f32(0.0f, 1.0f, 0.0f);
		GX_Color1u32(0);
		if (tex) GX_TexCoord2f32(0.0f, 0.0f);
	}
	GX_End();
}

void vdesc_pnc_flush(void)
{
	vdesc_pnc();
	prime_triangle(false);
}

void vdesc_pnct_flush(void)
{
	vdesc_pnct();
	prime_triangle(true);
}

void vdesc_2d(bool textured)
{
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	if (textured)
		GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
}

/* ------------------------------------------------------------------------ */
/* Light ramp                                                               */
/* ------------------------------------------------------------------------ */

/*
 * The ramp maps s = 0.5 + 0.5 * dot(N, L) to a colour. Values are stored
 * halved and the TEV doubles them, so light can exceed 1.0 (sunlit albedo).
 * The terminator is soft and slightly "wrapped", so form reads as two
 * painted tones rather than a photographic falloff.
 */
void ramp_update(const daylight *d)
{
	int next = s_ramp_cur ^ 1;
	u8 *buf = s_ramp_buf[next];
	rgbf amb = rgb_scale(d->ambient, 0.82f);
	rgbf shade = rgb_lerp(amb, rgb_mul(d->shadow, rgb_make(1.25f, 1.25f, 1.25f)), 0.38f);
	rgbf key = rgb_scale(d->sun, d->key_strength);

	u8 texel[64][4];
	for (int i = 0; i < 64; i++) {
		float s = (i + 0.5f) / 64.0f;
		float ndl = s * 2.0f - 1.0f;
		float lit = smoothstepf(-0.10f, 0.18f, ndl);
		rgbf light = rgb_add(amb, rgb_scale(key, 0.62f + 0.38f * saturatef(ndl)));
		rgbf c = rgb_lerp(shade, light, lit);
		texel[i][0] = (u8)(saturatef(c.r * 0.5f) * 255.0f);
		texel[i][1] = (u8)(saturatef(c.g * 0.5f) * 255.0f);
		texel[i][2] = (u8)(saturatef(c.b * 0.5f) * 255.0f);
		texel[i][3] = 255;
	}
	/* RGBA8 tiles: 4x4 texels; 32 bytes of AR pairs then 32 bytes of GB. */
	int o = 0;
	for (int ty = 0; ty < 1; ty++) {
		for (int tx = 0; tx < 16; tx++) {
			for (int k = 0; k < 16; k++) {
				int x = tx * 4 + (k & 3);
				buf[o + k * 2 + 0] = texel[x][3];
				buf[o + k * 2 + 1] = texel[x][0];
				buf[o + 32 + k * 2 + 0] = texel[x][1];
				buf[o + 32 + k * 2 + 1] = texel[x][2];
			}
			o += 64;
		}
	}
	DCFlushRange(buf, 64 * 4 * 4);
	s_ramp_cur = next;
}

/* ------------------------------------------------------------------------ */
/* Materials                                                                */
/* ------------------------------------------------------------------------ */

static void tev_ramp_stage(void)
{
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
	GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
}

static void lit_common(void)
{
	GX_SetNumChans(1);
	GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
	GX_SetNumTexGens(2);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_NRM, GX_TEXMTX0);
	GX_LoadTexObj(&s_ramp_obj[s_ramp_cur], GX_TEXMAP0);
	GX_SetNumTevStages(2);
	tev_ramp_stage();
}

void mat_lit_solid(void)
{
	lit_common();
	/* Paper tooth projected in world space: s=(x+z)/7, t=(y+z/2)/7. */
	Mtx p = { { 1.0f / 7.0f, 0.0f, 1.0f / 7.0f, 0.0f },
	          { 0.0f, 1.0f / 7.0f, 0.5f / 7.0f, 0.0f },
	          { 0.0f, 0.0f, 0.0f, 1.0f } };
	GX_LoadTexMtxImm(p, GX_TEXMTX1, GX_MTX2x4);
	GX_SetTexCoordGen(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_POS, GX_TEXMTX1);
	GX_LoadTexObj(&T_PAPER, GX_TEXMAP1);
	GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP1, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV, GX_CC_TEXC, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
	GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetZCompLoc(GX_TRUE);
	GX_SetCullMode(GFX_SOLID_CULL);
}

void mat_lit_cutout(void)
{
	lit_common();
	GX_SetTexCoordGen(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_LoadTexObj(&T_FOLIAGE, GX_TEXMAP1);
	GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP1, GX_COLORNULL);
	GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV, GX_CC_TEXC, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
	GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	/* Stage 2: per-instance tint from konst colour K0 (set with GX_SetTevKColor). */
	GX_SetNumTevStages(3);
	GX_SetTevOrder(GX_TEVSTAGE2, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL);
	GX_SetTevKColorSel(GX_TEVSTAGE2, GX_TEV_KCSEL_K0);
	GX_SetTevColorIn(GX_TEVSTAGE2, GX_CC_ZERO, GX_CC_CPREV, GX_CC_KONST, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE2, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
	GX_SetTevAlphaOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	/* Cut-paper edges: alpha test, with depth written only for kept texels. */
	GX_SetAlphaCompare(GX_GREATER, 120, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetZCompLoc(GX_FALSE);
	GX_SetCullMode(GX_CULL_NONE);
}

void mat_ramp_light(v3 l)
{
	/* s = 0.5 + 0.5 * dot(N, L); t fixed mid-texture. */
	Mtx m = { { 0.5f * l.x, 0.5f * l.y, 0.5f * l.z, 0.5f },
	          { 0.0f, 0.0f, 0.0f, 0.5f },
	          { 0.0f, 0.0f, 0.0f, 1.0f } };
	GX_LoadTexMtxImm(m, GX_TEXMTX0, GX_MTX2x4);
}

void mat_vcolor(void)
{
	GX_SetNumChans(1);
	GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
	GX_SetNumTexGens(0);
	GX_SetNumTevStages(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetZCompLoc(GX_TRUE);
	GX_SetCullMode(GX_CULL_NONE);
}

void mat_vcolor_tex(GXTexObj *t, bool tex_alpha_only, bool scale2)
{
	GX_SetNumChans(1);
	GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
	GX_SetNumTexGens(1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_LoadTexObj(t, GX_TEXMAP0);
	GX_SetNumTevStages(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	if (tex_alpha_only)   /* e.g. fonts, glow: colour from vertices, coverage from texture */
		GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
	else
		GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
	GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, scale2 ? GX_CS_SCALE_2 : GX_CS_SCALE_1,
	                 GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
	GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetZCompLoc(GX_TRUE);
	GX_SetCullMode(GX_CULL_NONE);
}

void gfx_blend_none(void) { GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR); }
void gfx_blend_alpha(void) { GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR); }
void gfx_blend_add(void) { GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR); }

void gfx_depth(bool test, bool write)
{
	GX_SetZMode(test ? GX_TRUE : GX_FALSE, GX_LEQUAL, write ? GX_TRUE : GX_FALSE);
}

void gfx_fog(const daylight *d)
{
	/* Perspective exp^2 haze: clear up close, thickening with distance. */
	float end = 380.0f / clampf(d->fog_density, 0.25f, 1.2f);
	GX_SetFog(GX_FOG_PERSP_EXP2, 0.0f, end, CAM_NEAR, CAM_FAR, gx_col(d->fog, 1.0f));
}

void gfx_fog_off(void)
{
	GX_SetFog(GX_FOG_NONE, 0.0f, 1.0f, CAM_NEAR, CAM_FAR, (GXColor){ 0, 0, 0, 0 });
}

/* ------------------------------------------------------------------------ */
/* 2D helpers and post                                                      */
/* ------------------------------------------------------------------------ */

void ui_rect_grad(float x, float y, float w, float h, GXColor top, GXColor bottom)
{
	vdesc_2d(false);
	GX_Begin(GX_QUADS, VF_2D, 4);
	GX_Position2f32(x, y);         GX_Color4u8(top.r, top.g, top.b, top.a);
	GX_Position2f32(x + w, y);     GX_Color4u8(top.r, top.g, top.b, top.a);
	GX_Position2f32(x + w, y + h); GX_Color4u8(bottom.r, bottom.g, bottom.b, bottom.a);
	GX_Position2f32(x, y + h);     GX_Color4u8(bottom.r, bottom.g, bottom.b, bottom.a);
	GX_End();
}

void ui_rect(float x, float y, float w, float h, GXColor c)
{
	ui_rect_grad(x, y, w, h, c, c);
}

void gfx_post(const daylight *d, float fade)
{
	gfx_ortho();
	gfx_depth(false, false);
	gfx_fog_off();
	float W = G.ui_w, H = G.ui_h;

	/* 1. Lift: a thin veil of warm paper colour raises the blacks. */
	mat_vcolor();
	gfx_blend_alpha();
	rgbf lift = rgb_lerp(rgb_make(0.91f, 0.87f, 0.81f), d->fog, 0.25f);
	ui_rect(0, 0, W, H, gx_col(lift, 0.06f));

	/* 2. Warm tint: multiply (dst * src). */
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_DSTCLR, GX_BL_ZERO, GX_LO_CLEAR);
	rgbf tint = rgb_lerp(rgb_make(1.0f, 0.965f, 0.918f), rgb_make(0.93f, 0.95f, 1.0f), d->night);
	ui_rect(0, 0, W, H, gx_col(tint, 1.0f));

	/* 3. Paper grain + vignette in one pass: dst = 2 * dst * src.
	 *    The grain texture is centred on 0.5, so 0.5 * vignette = neutral. */
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_DSTCLR, GX_BL_SRCCLR, GX_LO_CLEAR);
	mat_vcolor_tex(&T_GRAIN, false, false);
	vdesc_2d(true);
	const int NX = 6, NY = 4;
	float ox = (float)(hash32(G.frame / 2u) & 127) / 128.0f;
	float oy = (float)(hash32(G.frame / 2u + 77u) & 127) / 128.0f;
	float su = W / 128.0f, sv = H / 128.0f;
	for (int j = 0; j < NY; j++) {
		GX_Begin(GX_TRIANGLESTRIP, VF_2D, (NX + 1) * 2);
		for (int i = 0; i <= NX; i++) {
			for (int k = 0; k < 2; k++) {
				float u = (float)i / NX, v = (float)(j + k) / NY;
				float dx = (u - 0.5f) * 1.25f, dy = v - 0.5f;
				float r = sqrtf(dx * dx + dy * dy) / 0.72f;
				float vig = 1.0f - 0.22f * smoothstepf(0.45f, 1.05f, r) - 0.06f * fade;
				u8 c = (u8)(clampf(vig * (1.0f - fade), 0.0f, 1.0f) * 255.0f);
				GX_Position2f32(u * W, v * H);
				GX_Color4u8(c, c, c, 255);
				GX_TexCoord2f32(ox + u * su, oy + v * sv);
			}
		}
		GX_End();
	}
	gfx_blend_none();
}
