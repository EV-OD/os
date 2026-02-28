#ifndef GUI_CANVAS_H
#define GUI_CANVAS_H

/* =========================================================================
 * gui/canvas.h – Offscreen pixel-buffer drawing primitives
 *
 * All operations render into a caller-supplied pixel buffer
 *   unsigned int *buf  (row-major, 32-bpp 0x00RRGGBB)
 *   int cw, ch         (buffer dimensions in pixels)
 *
 * Coordinates: (0,0) = top-left.
 * Clipping:    pixels outside [0,cw)×[0,ch) are silently discarded.
 *
 * Text uses the embedded IBM-VGA 8×8 bitmap font (font8x8_basic).
 * Pass COLOR_TRANSPARENT as bg to skip background pixels.
 * ========================================================================= */

#include "gui/color.h"

/* -------------------------------------------------------------------------
 * Font metrics (matches font.h constants)
 * ------------------------------------------------------------------------- */
#define CNV_FONT_W       8
#define CNV_FONT_H       8
#define CNV_FONT_LINE_H  12

/* -------------------------------------------------------------------------
 * Clear / fill
 * ------------------------------------------------------------------------- */

/** Fill the entire canvas with one colour. */
void cnv_clear(unsigned int *buf, int cw, int ch, unsigned int col);

/** Fill a axis-aligned rectangle. */
void cnv_fill_rect(unsigned int *buf, int cw, int ch,
                   int x, int y, int w, int h, unsigned int col);

/** Fill a rectangle with a vertical gradient (top → bottom). */
void cnv_grad_v(unsigned int *buf, int cw, int ch,
                int x, int y, int w, int h,
                unsigned int top, unsigned int bot);

/** Filled rectangle with rounded corners (radius clamped to min(w,h)/2). */
void cnv_fill_round_rect(unsigned int *buf, int cw, int ch,
                         int x, int y, int w, int h,
                         int radius, unsigned int col);

/** Filled circle. */
void cnv_fill_circle(unsigned int *buf, int cw, int ch,
                     int cx, int cy, int r, unsigned int col);

/* -------------------------------------------------------------------------
 * Outlines / strokes
 * ------------------------------------------------------------------------- */

/** 1-px rectangle outline. */
void cnv_draw_rect(unsigned int *buf, int cw, int ch,
                   int x, int y, int w, int h, unsigned int col);

/** 1-px rounded rectangle outline. */
void cnv_draw_round_rect(unsigned int *buf, int cw, int ch,
                         int x, int y, int w, int h,
                         int radius, unsigned int col);

/** Bresenham line. */
void cnv_draw_line(unsigned int *buf, int cw, int ch,
                   int x0, int y0, int x1, int y1, unsigned int col);

/** Midpoint circle outline. */
void cnv_draw_circle(unsigned int *buf, int cw, int ch,
                     int cx, int cy, int r, unsigned int col);

/* -------------------------------------------------------------------------
 * Text (8×8 bitmap glyph, font8x8_basic)
 * ------------------------------------------------------------------------- */

/** Draw one ASCII character at (x, y). */
void cnv_draw_char(unsigned int *buf, int cw, int ch,
                   int x, int y, char c,
                   unsigned int fg, unsigned int bg);

/**
 * Draw a NUL-terminated string starting at (x, y).
 * '\n' advances y by CNV_FONT_LINE_H and resets x to x_origin.
 */
void cnv_draw_str(unsigned int *buf, int cw, int ch,
                  int x, int y, const char *s,
                  unsigned int fg, unsigned int bg);

/* -------------------------------------------------------------------------
 * Widgets (high-level composites drawn into the canvas)
 * ------------------------------------------------------------------------- */

/**
 * Render a filled button-style widget:
 *   - rounded filled background (bg_col)
 *   - 1-px border (border_col, darker)
 *   - text centred inside
 */
void cnv_button(unsigned int *buf, int cw, int ch,
                int x, int y, int w, int h,
                const char *label,
                unsigned int fg, unsigned int bg, unsigned int border);

/**
 * Render a label (text with optional filled background).
 * Pass COLOR_TRANSPARENT for bg to skip the background fill.
 */
void cnv_label(unsigned int *buf, int cw, int ch,
               int x, int y, const char *text,
               unsigned int fg, unsigned int bg);

/**
 * Render a horizontal separator line (1 px).
 */
void cnv_hsep(unsigned int *buf, int cw, int ch,
              int x, int y, int w, unsigned int col);

/**
 * Render a progress bar:
 *   value in [0, max].  Filled from left, outlined.
 */
void cnv_progress(unsigned int *buf, int cw, int ch,
                  int x, int y, int w, int h,
                  int value, int max_val,
                  unsigned int fill_col, unsigned int bg_col, unsigned int border_col);

/* -------------------------------------------------------------------------
 * Layout helpers  (pure arithmetic, return pixel coordinates)
 * -------------------------------------------------------------------------
 * Grid layout  ─  divides a rectangular region into equal-sized cells.
 *
 *   region origin: (rx, ry),  total size: (rw, rh)
 *   padding (px, py): space between the region edge and the first cell
 *   gap:   space between adjacent cells
 *   cols / rows: number of columns / rows
 *
 * Cell (c, r): top-left = (grid_cell_x(...), grid_cell_y(...))
 *              size      = (grid_cell_w(...), grid_cell_h(...))
 * ------------------------------------------------------------------------- */

/** X coordinate of cell column c (0-based). */
static inline int grid_cell_x(int rx, int rw, int cols, int px, int gap, int c)
{
    int cell_w = (rw - 2*px - gap*(cols-1)) / cols;
    return rx + px + c * (cell_w + gap);
}

/** Y coordinate of cell row r (0-based). */
static inline int grid_cell_y(int ry, int rh, int rows, int py, int gap, int r)
{
    int cell_h = (rh - 2*py - gap*(rows-1)) / rows;
    return ry + py + r * (cell_h + gap);
}

/** Width of one cell (or colspan cells merged). */
static inline int grid_cell_w(int rw, int cols, int px, int gap, int colspan)
{
    int cell_w = (rw - 2*px - gap*(cols-1)) / cols;
    return cell_w * colspan + gap * (colspan - 1);
}

/** Height of one cell (or rowspan cells merged). */
static inline int grid_cell_h(int rh, int rows, int py, int gap, int rowspan)
{
    int cell_h = (rh - 2*py - gap*(rows-1)) / rows;
    return cell_h * rowspan + gap * (rowspan - 1);
}

/* -------------------------------------------------------------------------
 * Flex-row layout  ─  evenly spaces items horizontally.
 *
 *   total width: w,  padding: pad each side,  gap between items,  n items
 *
 * Item i (0-based): left = flex_item_x(...),  width = flex_item_w(...)
 * ------------------------------------------------------------------------- */

/** X coordinate of the i-th flex item. */
static inline int flex_item_x(int x, int w, int n, int pad, int gap, int i)
{
    int item_w = (n > 0) ? (w - 2*pad - gap*(n-1)) / n : 0;
    return x + pad + i * (item_w + gap);
}

/** Width of one flex item. */
static inline int flex_item_w(int w, int n, int pad, int gap)
{
    return (n > 0) ? (w - 2*pad - gap*(n-1)) / n : 0;
}

/** Y coordinate of the i-th flex-column item (vertical flex). */
static inline int flex_item_y(int y, int h, int n, int pad, int gap, int i)
{
    int item_h = (n > 0) ? (h - 2*pad - gap*(n-1)) / n : 0;
    return y + pad + i * (item_h + gap);
}

/** Height of one vertical flex item. */
static inline int flex_item_h(int h, int n, int pad, int gap)
{
    return (n > 0) ? (h - 2*pad - gap*(n-1)) / n : 0;
}

#endif /* GUI_CANVAS_H */
