#ifndef GUI_FONT_H
#define GUI_FONT_H

/* =========================================================================
 * gui/font.h – Embedded 8×8 bitmap font
 *
 * Uses the public-domain font8x8_basic library (author: Daniel Hepper,
 * based on IBM VGA ROM font data) for glyph data.
 * Vendor header: gui/vendor/font8x8_basic.h
 *
 * Glyph layout (font8x8_basic):
 *   Each glyph is 8 bytes (one per row, top→bottom).
 *   Bit 7 of each byte = leftmost pixel (x offset 0).
 *   Bit 0 of each byte = rightmost pixel (x offset 7).
 *   Covers ASCII 0x00–0x7F (128 glyphs).
 * ========================================================================= */

#include "gui/color.h"

/* Width and height of every glyph in pixels. */
#define FONT_W  8
#define FONT_H  8

/* -------------------------------------------------------------------------
 * Rendering helpers
 * All coordinates are screen pixels; writes go to the FB back-buffer.
 * Passing COLOR_TRANSPARENT as @p bg skips background pixels.
 * ------------------------------------------------------------------------- */

/**
 * Draw a single character glyph at (x, y).
 *
 * @param x, y  Top-left pixel of the glyph.
 * @param c     Character to draw (ASCII 0-127 from font8x8_basic;
 *              values >= 128 render as a block).
 * @param fg    Foreground colour.
 * @param bg    Background colour, or COLOR_TRANSPARENT to skip bg pixels.
 */
void font_draw_char(int x, int y, char c, color_t fg, color_t bg);

/**
 * Draw a NUL-terminated string starting at (x, y).
 * Newline '\n' advances y by FONT_H and resets x to @p x_origin.
 * '\r' resets x to @p x_origin.
 * '\b' moves x back by FONT_W (min 0).
 *
 * @param x, y     Starting position.
 * @param s        NUL-terminated string.
 * @param fg, bg   Colours (bg may be COLOR_TRANSPARENT).
 */
void font_draw_str(int x, int y, const char *s, color_t fg, color_t bg);

/**
 * Return the pixel width of @p s when rendered with this font.
 * (Handles only single-line strings; stops at '\0' or '\n'.)
 */
int font_str_width(const char *s);

#endif /* GUI_FONT_H */
