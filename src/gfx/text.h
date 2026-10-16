/*
 * text.h -- "Lookout Hand" text rendering on the UI canvas.
 *
 * Strings are UTF-8. Each glyph has two hand-drawn variants; which one is
 * used depends on the character and its position, so repeated letters in a
 * word never look stamped.
 */
#ifndef LL_TEXT_H
#define LL_TEXT_H

#include <gccore.h>
#include <stdbool.h>

typedef enum { FONT_S, FONT_L } font_id;

float text_width(font_id f, const char *s, int len);       /* len < 0: whole string */
float text_line_height(font_id f);
float text_ascent(font_id f);
/* Draws at baseline y. Call text_begin() once before a batch of draws. */
void text_begin(void);
void text_draw(font_id f, float x, float y, const char *s, int len, GXColor c);
/* Soft drop shadow, then the text: legible over any sky. */
void text_draw_soft(font_id f, float x, float y, const char *s, int len, GXColor c, float shadow_a);

/* Greedy word wrap. Fills starts/lengths; returns the line count. */
int text_wrap(font_id f, const char *s, float max_w, int *starts, int *lens, int max_lines);

#endif
