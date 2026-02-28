/* =========================================================================
 * gui/gfx.c – Software drawing primitives
 *
 * All operations render into the framebuffer back-buffer via fb_put_pixel()
 * and fb_blit().  Nothing is sent to the screen until fb_flush*() is called.
 * ========================================================================= */

#include "gui/gfx.h"
#include "gui/fb.h"
#include "gui/font.h"
#include "gui/color.h"

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static inline int gfx_abs(int v)      { return v < 0 ? -v : v; }
static inline int gfx_min(int a, int b) { return a < b ? a  : b; }
static inline int gfx_max(int a, int b) { return a > b ? a  : b; }
static inline int gfx_clamp(int v, int lo, int hi)
    { return v < lo ? lo : (v > hi ? hi : v); }

/* Linearly interpolate between two 8-bit channel values. */
static inline unsigned char lerp8(unsigned char a, unsigned char b,
                                   unsigned int t, unsigned int total)
{
    if (total == 0) return a;
    return (unsigned char)(a + (int)((int)(b - a) * (int)t / (int)total));
}

/* -------------------------------------------------------------------------
 * Filled rectangle
 * ------------------------------------------------------------------------- */

void gfx_fill_rect(int x, int y, int w, int h, color_t color)
{
    int r, c;
    if (w <= 0 || h <= 0) return;

    for (r = 0; r < h; r++) {
        for (c = 0; c < w; c++) {
            fb_put_pixel(x + c, y + r, color);
        }
    }
}

/* -------------------------------------------------------------------------
 * Outlined rectangle (1 px border)
 * ------------------------------------------------------------------------- */

void gfx_draw_rect(int x, int y, int w, int h, color_t color)
{
    int i;
    if (w <= 0 || h <= 0) return;

    /* Top and bottom edges */
    for (i = 0; i < w; i++) {
        fb_put_pixel(x + i, y,         color);
        fb_put_pixel(x + i, y + h - 1, color);
    }
    /* Left and right edges */
    for (i = 0; i < h; i++) {
        fb_put_pixel(x,         y + i, color);
        fb_put_pixel(x + w - 1, y + i, color);
    }
}

/* -------------------------------------------------------------------------
 * Line (Bresenham's algorithm)
 * ------------------------------------------------------------------------- */

void gfx_draw_line(int x0, int y0, int x1, int y1, color_t color)
{
    int dx =  gfx_abs(x1 - x0);
    int dy = -gfx_abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    int e2;

    for (;;) {
        fb_put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* -------------------------------------------------------------------------
 * Circle outline (Bresenham midpoint algorithm)
 * ------------------------------------------------------------------------- */

static void plot8(int cx, int cy, int x, int y, color_t c)
{
    fb_put_pixel(cx + x, cy + y, c); fb_put_pixel(cx - x, cy + y, c);
    fb_put_pixel(cx + x, cy - y, c); fb_put_pixel(cx - x, cy - y, c);
    fb_put_pixel(cx + y, cy + x, c); fb_put_pixel(cx - y, cy + x, c);
    fb_put_pixel(cx + y, cy - x, c); fb_put_pixel(cx - y, cy - x, c);
}

void gfx_draw_circle(int cx, int cy, int r, color_t color)
{
    int x = 0, y = r;
    int d = 3 - 2 * r;
    if (r <= 0) return;
    while (y >= x) {
        plot8(cx, cy, x, y, color);
        x++;
        if (d > 0) { y--; d += 4 * (x - y) + 10; }
        else          d += 4 * x + 6;
    }
}

/* -------------------------------------------------------------------------
 * Filled ellipse (scanline)
 * ------------------------------------------------------------------------- */

void gfx_fill_ellipse(int cx, int cy, int rx, int ry, color_t color)
{
    int y;
    long rx2 = (long)rx * rx;
    long ry2 = (long)ry * ry;

    if (rx <= 0 || ry <= 0) return;

    for (y = -ry; y <= ry; y++) {
        /* Compute x half-width at this row: x = rx * sqrt(1 - (y/ry)^2) */
        long rhs = rx2 - rx2 * (long)y * y / ry2;
        if (rhs < 0) continue;
        /* Integer square root (Newton's method) */
        long x_hw = rx;
        long tmp;
        do {
            tmp  = x_hw;
            x_hw = (tmp + rhs / tmp) / 2;
        } while (x_hw < tmp);
        gfx_fill_rect(cx - (int)x_hw, cy + y, (int)(2 * x_hw + 1), 1, color);
    }
}

void gfx_fill_circle(int cx, int cy, int r, color_t color)
{
    gfx_fill_ellipse(cx, cy, r, r, color);
}

/* -------------------------------------------------------------------------
 * Rounded rectangle (filled)
 * ------------------------------------------------------------------------- */

void gfx_fill_round_rect(int x, int y, int w, int h, int radius, color_t color)
{
    int max_r, r, cx, cy, xr;
    if (w <= 0 || h <= 0) return;

    max_r = gfx_min(w, h) / 2;
    if (radius > max_r) radius = max_r;
    if (radius <= 0) { gfx_fill_rect(x, y, w, h, color); return; }

    /* Centre strip (full width) */
    gfx_fill_rect(x, y + radius, w, h - 2 * radius, color);
    /* Top and bottom strips (without corners) */
    gfx_fill_rect(x + radius, y,             w - 2 * radius, radius, color);
    gfx_fill_rect(x + radius, y + h - radius, w - 2 * radius, radius, color);

    /* Four corners using circle fill per row */
    for (r = 0; r < radius; r++) {
        /* Distance from centre to edge of circle at this row */
        long r2 = (long)radius * radius;
        long dy = radius - r - 1;
        long x_hw2 = r2 - dy * dy;
        if (x_hw2 < 0) continue;
        long x_hw = radius;
        long tmp;
        do { tmp = x_hw; x_hw = (tmp + x_hw2 / tmp) / 2; } while (x_hw < tmp);
        xr = (int)x_hw;

        cx = x + radius;
        cy = y + r;
        gfx_fill_rect(cx - xr, cy, xr, 1, color);

        cx = x + w - radius;
        gfx_fill_rect(cx, cy, xr, 1, color);

        cy = y + h - r - 1;
        cx = x + radius;
        gfx_fill_rect(cx - xr, cy, xr, 1, color);

        cx = x + w - radius;
        gfx_fill_rect(cx, cy, xr, 1, color);
    }
    (void)cx; /* suppress unused warning */
}

/* -------------------------------------------------------------------------
 * Vertical gradient fill
 * ------------------------------------------------------------------------- */

void gfx_fill_gradient_v(int x, int y, int w, int h,
                          color_t top, color_t bottom)
{
    int row;
    unsigned char tr = COLOR_R(top),    tg = COLOR_G(top),    tb = COLOR_B(top);
    unsigned char br = COLOR_R(bottom), bg = COLOR_G(bottom), bb = COLOR_B(bottom);

    for (row = 0; row < h; row++) {
        color_t c = COLOR_RGB(
            lerp8(tr, br, (unsigned int)row, (unsigned int)(h - 1)),
            lerp8(tg, bg, (unsigned int)row, (unsigned int)(h - 1)),
            lerp8(tb, bb, (unsigned int)row, (unsigned int)(h - 1))
        );
        gfx_fill_rect(x, y + row, w, 1, c);
    }
}

/* -------------------------------------------------------------------------
 * Pixel blit
 * ------------------------------------------------------------------------- */

void gfx_blit(int dst_x, int dst_y, int w, int h,
              const unsigned int *src, int src_stride)
{
    fb_blit(dst_x, dst_y, w, h, src, src_stride);
}

/* -------------------------------------------------------------------------
 * Text
 * ------------------------------------------------------------------------- */

void gfx_draw_text(int x, int y, const char *s, color_t fg, color_t bg)
{
    font_draw_str(x, y, s, fg, bg);
}
