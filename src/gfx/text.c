/*
 * text.c -- glyph quads from the Lookout Hand atlases (gen/font_*.h).
 */
#include <string.h>
#include "gfx/text.h"
#include "gfx/gfx.h"

typedef struct {
	u16 x, y;
	u8 w, h;
	s8 xoff, yoff;
	u16 adv8;              /* advance in 1/8 px */
} ll_glyph;

#include "font_slots.h"
#include "font_small.h"
#include "font_large.h"

typedef struct {
	const ll_glyph *glyphs;
	GXTexObj *tex;
	float inv_w, inv_h;
	float line_h, ascent;
} font_t;

static font_t s_fonts[2] = {
	{ font_small_glyphs, &T_FONT_S, 1.0f / FONT_SMALL_TEX_W, 1.0f / FONT_SMALL_TEX_H, FONT_SMALL_LINE_H, FONT_SMALL_ASCENT },
	{ font_large_glyphs, &T_FONT_L, 1.0f / FONT_LARGE_TEX_W, 1.0f / FONT_LARGE_TEX_H, FONT_LARGE_LINE_H, FONT_LARGE_ASCENT },
};

/* Decode one UTF-8 code point; malformed or truncated input yields '?'. */
static unsigned utf8_next(const char **p, const char *end)
{
	const unsigned char *s = (const unsigned char *)*p;
	unsigned c = s[0];
	int n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
	if (*p + n > end) {
		*p = end;
		return '?';
	}
	if (n == 4) c = ((c & 7) << 18) | ((s[1] & 63) << 12) | ((s[2] & 63) << 6) | (s[3] & 63);
	else if (n == 3) c = ((c & 15) << 12) | ((s[1] & 63) << 6) | (s[2] & 63);
	else if (n == 2) c = ((c & 31) << 6) | (s[1] & 63);
	*p += n;
	return c;
}

static const ll_glyph *glyph(const font_t *f, unsigned cp, int index)
{
	int slot = font_slot(cp);
	int variant = (int)((cp * 7u + (unsigned)index * 3u) >> 1) & 1;
	return &f->glyphs[slot * FONT_VARIANTS + variant];
}

float text_line_height(font_id f) { return s_fonts[f].line_h; }
float text_ascent(font_id f) { return s_fonts[f].ascent; }

float text_width(font_id fid, const char *s, int len)
{
	const font_t *f = &s_fonts[fid];
	const char *end = s + (len < 0 ? (int)strlen(s) : len);
	float w = 0.0f;
	int i = 0;
	while (s < end) {
		unsigned cp = utf8_next(&s, end);
		w += glyph(f, cp, i++)->adv8 * 0.125f;
	}
	return w;
}

void text_begin(void)
{
	gfx_ortho();
	gfx_depth(false, false);
	gfx_blend_alpha();
}

void text_draw(font_id fid, float x, float y, const char *s, int len, GXColor c)
{
	const font_t *f = &s_fonts[fid];
	const char *end = s + (len < 0 ? (int)strlen(s) : len);
	mat_vcolor_tex(f->tex, true, false);
	vdesc_2d(true);
	float pen = x;
	int i = 0;
	while (s < end) {
		unsigned cp = utf8_next(&s, end);
		const ll_glyph *g = glyph(f, cp, i++);
		if (cp != ' ') {
			float x0 = pen + g->xoff, y0 = y + g->yoff;
			float x1 = x0 + g->w, y1 = y0 + g->h;
			float u0 = g->x * f->inv_w, v0 = g->y * f->inv_h;
			float u1 = (g->x + g->w) * f->inv_w, v1 = (g->y + g->h) * f->inv_h;
			GX_Begin(GX_QUADS, VF_2D, 4);
			GX_Position2f32(x0, y0); GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(u0, v0);
			GX_Position2f32(x1, y0); GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(u1, v0);
			GX_Position2f32(x1, y1); GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(u1, v1);
			GX_Position2f32(x0, y1); GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(u0, v1);
			GX_End();
		}
		pen += g->adv8 * 0.125f;
	}
}

void text_draw_soft(font_id f, float x, float y, const char *s, int len, GXColor c, float shadow_a)
{
	/* Four offset passes approximate a soft blurred shadow under the pencil. */
	u8 a = (u8)(shadow_a * c.a * 0.28f);
	GXColor sh = { 20, 18, 22, a };
	static const float off[4][2] = { { 1.5f, 1.5f }, { 0.5f, 2.0f }, { 2.0f, 0.5f }, { 1.0f, 1.0f } };
	for (int i = 0; i < 4; i++)
		text_draw(f, x + off[i][0], y + off[i][1], s, len, sh);
	text_draw(f, x, y, s, len, c);
}

int text_wrap(font_id fid, const char *s, float max_w, int *starts, int *lens, int max_lines)
{
	int n = 0, len = (int)strlen(s);
	int line_start = 0;
	while (line_start < len && n < max_lines) {
		while (line_start < len && s[line_start] == ' ') line_start++;
		int best = -1;
		int i = line_start;
		while (i <= len) {
			if (i == len || s[i] == ' ' || s[i] == '\n') {
				float w = text_width(fid, s + line_start, i - line_start);
				if (w > max_w && best >= 0) break;
				best = i;
				if (i == len || s[i] == '\n') break;
			}
			i++;
		}
		if (best < 0) best = len;
		starts[n] = line_start;
		lens[n] = best - line_start;
		n++;
		line_start = best + 1;
	}
	return n;
}
