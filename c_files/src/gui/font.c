/* =========================================================================
 * gui/font.c – 8×8 bitmap font rendering using font8x8_basic
 *
 * font8x8_basic is a public-domain single-header library providing 128
 * 8×8 glyphs for ASCII U+0000–U+007F (author: Daniel Hepper, IBM VGA font).
 *
 * Bit layout per row byte: bit 7 = leftmost pixel (column 0).
 * ========================================================================= */

/* Pull in the vendor glyph data (defines static char font8x8_basic[128][8]) */
#include "gui/vendor/font8x8_basic.h"

#include "gui/font.h"
#include "gui/fb.h"

/* -------------------------------------------------------------------------
 * Internal: draw one glyph row
 * ------------------------------------------------------------------------- */

static inline void draw_glyph_row(int x, int y, unsigned char row_bits,
                                   color_t fg, color_t bg)
{
    int col;
    for (col = 0; col < FONT_W; col++) {
        /* Bit 7-col is the pixel at column col (0=left). */
        if (row_bits & (0x80u >> col)) {
            fb_put_pixel(x + col, y, fg);
        } else if (bg != COLOR_TRANSPARENT) {
            fb_put_pixel(x + col, y, bg);
        }
    }
}

/* -------------------------------------------------------------------------
 * font_draw_char
 * ------------------------------------------------------------------------- */

void font_draw_char(int x, int y, char c, color_t fg, color_t bg)
{
    unsigned int idx = (unsigned char)c;
    int row;

    /* Clamp to the glyph table; use glyph 0 (NUL = blank) for out-of-range. */
    if (idx >= 128u)
        idx = 0;

    for (row = 0; row < FONT_H; row++) {
        unsigned char bits = (unsigned char)font8x8_basic[idx][row];
        draw_glyph_row(x, y + row, bits, fg, bg);
    }
}

/* -------------------------------------------------------------------------
 * font_draw_str
 * ------------------------------------------------------------------------- */

void font_draw_str(int x, int y, const char *s, color_t fg, color_t bg)
{
    int x_origin = x;

    if (!s) return;

    while (*s) {
        char c = *s++;
        switch (c) {
        case '\n':
            y += FONT_H;
            x  = x_origin;
            break;
        case '\r':
            x = x_origin;
            break;
        case '\b':
            x -= FONT_W;
            if (x < x_origin) x = x_origin;
            break;
        case '\t':
            /* Advance to next 8-column tab stop (relative to x_origin). */
            {
                int col = (x - x_origin) / FONT_W;
                int next_tab = ((col / 8) + 1) * 8;
                x = x_origin + next_tab * FONT_W;
            }
            break;
        default:
            font_draw_char(x, y, c, fg, bg);
            x += FONT_W;
            break;
        }
    }
}

/* -------------------------------------------------------------------------
 * font_str_width
 * ------------------------------------------------------------------------- */

int font_str_width(const char *s)
{
    int w = 0;
    if (!s) return 0;
    while (*s && *s != '\n') {
        w += FONT_W;
        s++;
    }
    return w;
}
