#ifndef GUI_GFX_H
#define GUI_GFX_H

/* =========================================================================
 * gui/gfx.h – Software drawing primitives
 *
 * All functions render into the global framebuffer BACK-BUFFER via
 * fb_put_pixel().  Nothing is flushed to the screen until fb_flush() (or
 * fb_flush_rect()) is called.
 *
 * Coordinate system: (0,0) = top-left corner of the screen.
 * Clipping: all operations are silently clipped to [0, fb_width()) ×
 *           [0, fb_height()); no assertion / crash for out-of-bounds coords.
 * ========================================================================= */

#include "gui/color.h"

/* -------------------------------------------------------------------------
 * Filled shapes
 * ------------------------------------------------------------------------- */

/**
 * Fill a solid rectangle.
 * @param x, y   Top-left corner.
 * @param w, h   Width and height (0 = no-op).
 * @param color  Fill colour.
 */
void gfx_fill_rect(int x, int y, int w, int h, color_t color);

/**
 * Fill an ellipse/circle.
 * @param cx, cy  Centre pixel.
 * @param rx, ry  X and Y radii in pixels.
 * @param color   Fill colour.
 */
void gfx_fill_ellipse(int cx, int cy, int rx, int ry, color_t color);

/** Convenience wrapper: filled circle with equal X/Y radius. */
void gfx_fill_circle(int cx, int cy, int r, color_t color);

/* -------------------------------------------------------------------------
 * Outlines / stroked shapes
 * ------------------------------------------------------------------------- */

/**
 * Draw a 1-pixel-wide rectangle outline.
 * @param x, y   Top-left corner.
 * @param w, h   Width and height.
 * @param color  Line colour.
 */
void gfx_draw_rect(int x, int y, int w, int h, color_t color);

/**
 * Draw a line using Bresenham's algorithm.
 * @param x0, y0  Start pixel.
 * @param x1, y1  End pixel.
 * @param color   Line colour.
 */
void gfx_draw_line(int x0, int y0, int x1, int y1, color_t color);

/**
 * Draw a circle outline (1-pixel stroke) using the midpoint algorithm.
 * @param cx, cy  Centre pixel.
 * @param r       Radius in pixels.
 * @param color   Line colour.
 */
void gfx_draw_circle(int cx, int cy, int r, color_t color);

/* -------------------------------------------------------------------------
 * Rounded rectangle (for window decorations, buttons)
 * ------------------------------------------------------------------------- */

/**
 * Draw a filled rectangle with rounded corners.
 * @param x, y    Top-left.
 * @param w, h    Size.
 * @param radius  Corner radius; clamped to min(w,h)/2.
 * @param color   Fill colour.
 */
void gfx_fill_round_rect(int x, int y, int w, int h, int radius, color_t color);

/* -------------------------------------------------------------------------
 * Gradient fill (horizontal or vertical linear gradient)
 * ------------------------------------------------------------------------- */

/**
 * Fill a rectangle with a vertical gradient from @p top to @p bottom.
 */
void gfx_fill_gradient_v(int x, int y, int w, int h,
                          color_t top, color_t bottom);

/* -------------------------------------------------------------------------
 * Pixel blit (src → back-buffer)
 * ------------------------------------------------------------------------- */

/**
 * Copy an array of pixels into the back-buffer.
 * @param dst_x, dst_y  Top-left destination in the back-buffer.
 * @param w, h          Rectangle size.
 * @param src           Source pixel array (row-major, 32-bpp 0x00RRGGBB).
 *                      Pixels with value COLOR_TRANSPARENT are skipped.
 * @param src_stride    Number of 32-bit words per row in @p src.
 */
void gfx_blit(int dst_x, int dst_y, int w, int h,
              const unsigned int *src, int src_stride);

/* -------------------------------------------------------------------------
 * Text (thin wrappers around font.h)
 * ------------------------------------------------------------------------- */

/**
 * Draw a NUL-terminated string to the back-buffer.
 * @param x, y   Top-left of the first character.
 * @param s      String.
 * @param fg     Foreground colour.
 * @param bg     Background colour, or COLOR_TRANSPARENT.
 */
void gfx_draw_text(int x, int y, const char *s, color_t fg, color_t bg);

#endif /* GUI_GFX_H */
