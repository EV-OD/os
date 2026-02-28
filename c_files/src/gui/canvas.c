/* =========================================================================
 * gui/canvas.c – Offscreen pixel-buffer drawing primitives
 *
 * All functions operate on a caller-supplied unsigned int * buffer in
 * row-major order (pixel at (x,y) = buf[y*cw + x]).
 * Clipping is applied per-pixel via the inline cnv_set() helper so that
 * no routine can write outside the buffer.
 * ========================================================================= */

#include "gui/canvas.h"
#include "gui/color.h"
extern char font8x8_basic[128][8];           /* defined in gui/font.c */

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

static inline void cnv_set(unsigned int *buf, int cw, int ch,
                            int x, int y, unsigned int col)
{
    if ((unsigned int)x < (unsigned int)cw &&
        (unsigned int)y < (unsigned int)ch)
        buf[y * cw + x] = col;
}

static inline int cnv_abs(int v)          { return v < 0 ? -v : v; }
static inline int cnv_min(int a, int b)   { return a < b ? a : b; }
static inline int cnv_max(int a, int b)   { return a > b ? a : b; }
static inline int cnv_clamp(int v, int lo, int hi)
    { return v < lo ? lo : (v > hi ? hi : v); }

/* Integer square root (Newton's method). */
static inline int cnv_isqrt(long n)
{
    if (n <= 0) return 0;
    long x = n;
    long t;
    do { t = x; x = (t + n / t) / 2; } while (x < t);
    return (int)t;
}

/* Lerp between two 8-bit channels. */
static inline unsigned char lerp8(unsigned char a, unsigned char b,
                                   int t, int total)
{
    if (total <= 0) return a;
    return (unsigned char)(a + (b - a) * t / total);
}

/* =========================================================================
 * Clear / fill
 * ========================================================================= */

void cnv_clear(unsigned int *buf, int cw, int ch, unsigned int col)
{
    int n = cw * ch;
    for (int i = 0; i < n; i++) buf[i] = col;
}

void cnv_fill_rect(unsigned int *buf, int cw, int ch,
                   int x, int y, int w, int h, unsigned int col)
{
    if (w <= 0 || h <= 0) return;
    int x1 = cnv_clamp(x, 0, cw);
    int y1 = cnv_clamp(y, 0, ch);
    int x2 = cnv_clamp(x + w, 0, cw);
    int y2 = cnv_clamp(y + h, 0, ch);
    for (int row = y1; row < y2; row++) {
        unsigned int *p = buf + row * cw + x1;
        for (int col2 = x1; col2 < x2; col2++, p++)
            *p = col;
    }
}

void cnv_grad_v(unsigned int *buf, int cw, int ch,
                int x, int y, int w, int h,
                unsigned int top, unsigned int bot)
{
    if (w <= 0 || h <= 0) return;
    unsigned char tr = COLOR_R(top), tg = COLOR_G(top), tb = COLOR_B(top);
    unsigned char br = COLOR_R(bot),  bg2= COLOR_G(bot), bb = COLOR_B(bot);
    for (int row = 0; row < h; row++) {
        unsigned int c = COLOR_RGB(
            lerp8(tr, br, row, h - 1),
            lerp8(tg, bg2, row, h - 1),
            lerp8(tb, bb, row, h - 1));
        cnv_fill_rect(buf, cw, ch, x, y + row, w, 1, c);
    }
}

/* Filled circle (scanline method using integer sqrt). */
void cnv_fill_circle(unsigned int *buf, int cw, int ch,
                     int cx, int cy, int r, unsigned int col)
{
    if (r <= 0) return;
    long r2 = (long)r * r;
    for (int dy = -r; dy <= r; dy++) {
        long x_hw2 = r2 - (long)dy * dy;
        if (x_hw2 < 0) continue;
        int xhw = cnv_isqrt(x_hw2);
        cnv_fill_rect(buf, cw, ch, cx - xhw, cy + dy, 2 * xhw + 1, 1, col);
    }
}

/* Filled rounded rectangle (mathematically computed, no loops other than fill). */
void cnv_fill_round_rect(unsigned int *buf, int cw, int ch,
                         int x, int y, int w, int h,
                         int radius, unsigned int col)
{
    if (w <= 0 || h <= 0) return;
    int max_r = cnv_min(w, h) / 2;
    if (radius > max_r) radius = max_r;
    if (radius <= 0) { cnv_fill_rect(buf, cw, ch, x, y, w, h, col); return; }

    /* Centre vertical strip */
    cnv_fill_rect(buf, cw, ch, x, y + radius, w, h - 2 * radius, col);
    /* Top / bottom strips (between corners) */
    cnv_fill_rect(buf, cw, ch, x + radius, y,             w - 2*radius, radius, col);
    cnv_fill_rect(buf, cw, ch, x + radius, y + h - radius, w - 2*radius, radius, col);

    /* Four corners: per-row scanline fill */
    long r2 = (long)radius * radius;
    for (int row = 0; row < radius; row++) {
        long dy = radius - row - 1;
        long x_hw2 = r2 - dy * dy;
        if (x_hw2 < 0) continue;
        int xhw = cnv_isqrt(x_hw2);

        /* top-left corner */
        cnv_fill_rect(buf, cw, ch, x + radius - xhw, y + row, xhw, 1, col);
        /* top-right corner */
        cnv_fill_rect(buf, cw, ch, x + w - radius,   y + row, xhw, 1, col);
        /* bottom-left corner */
        cnv_fill_rect(buf, cw, ch, x + radius - xhw, y + h - 1 - row, xhw, 1, col);
        /* bottom-right corner */
        cnv_fill_rect(buf, cw, ch, x + w - radius,   y + h - 1 - row, xhw, 1, col);
    }
}

/* =========================================================================
 * Outlines
 * ========================================================================= */

void cnv_draw_rect(unsigned int *buf, int cw, int ch,
                   int x, int y, int w, int h, unsigned int col)
{
    if (w <= 0 || h <= 0) return;
    /* top / bottom */
    cnv_fill_rect(buf, cw, ch, x, y,         w, 1, col);
    cnv_fill_rect(buf, cw, ch, x, y + h - 1, w, 1, col);
    /* left / right */
    cnv_fill_rect(buf, cw, ch, x,         y, 1, h, col);
    cnv_fill_rect(buf, cw, ch, x + w - 1, y, 1, h, col);
}

void cnv_draw_round_rect(unsigned int *buf, int cw, int ch,
                         int x, int y, int w, int h,
                         int radius, unsigned int col)
{
    if (w <= 0 || h <= 0) return;
    int max_r = cnv_min(w, h) / 2;
    if (radius > max_r) radius = max_r;
    if (radius <= 0) { cnv_draw_rect(buf, cw, ch, x, y, w, h, col); return; }

    /* Straight edges (skip corners) */
    cnv_fill_rect(buf, cw, ch, x + radius, y,         w - 2*radius, 1, col); /* top */
    cnv_fill_rect(buf, cw, ch, x + radius, y + h - 1, w - 2*radius, 1, col); /* bottom */
    cnv_fill_rect(buf, cw, ch, x,         y + radius, 1, h - 2*radius, col); /* left */
    cnv_fill_rect(buf, cw, ch, x + w - 1, y + radius, 1, h - 2*radius, col); /* right */

    /* Four arc corners using midpoint algorithm */
    int cx_tl = x + radius,   cy_tl = y + radius;
    int cx_tr = x + w - 1 - radius, cy_tr = y + radius;
    int cx_bl = x + radius,   cy_bl = y + h - 1 - radius;
    int cx_br = x + w - 1 - radius, cy_br = y + h - 1 - radius;

    int px = 0, py = radius;
    int d = 3 - 2 * radius;
    while (py >= px) {
        /* top-left */
        cnv_set(buf, cw, ch, cx_tl - px, cy_tl - py, col);
        cnv_set(buf, cw, ch, cx_tl - py, cy_tl - px, col);
        /* top-right */
        cnv_set(buf, cw, ch, cx_tr + px, cy_tr - py, col);
        cnv_set(buf, cw, ch, cx_tr + py, cy_tr - px, col);
        /* bottom-left */
        cnv_set(buf, cw, ch, cx_bl - px, cy_bl + py, col);
        cnv_set(buf, cw, ch, cx_bl - py, cy_bl + px, col);
        /* bottom-right */
        cnv_set(buf, cw, ch, cx_br + px, cy_br + py, col);
        cnv_set(buf, cw, ch, cx_br + py, cy_br + px, col);
        px++;
        if (d > 0) { py--; d += 4 * (px - py) + 10; }
        else         d += 4 * px + 6;
    }
}

/* Bresenham line. */
void cnv_draw_line(unsigned int *buf, int cw, int ch,
                   int x0, int y0, int x1, int y1, unsigned int col)
{
    int dx =  cnv_abs(x1 - x0);
    int dy = -cnv_abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        cnv_set(buf, cw, ch, x0, y0, col);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Midpoint circle outline. */
static void plot8_cnv(unsigned int *buf, int cw, int ch,
                      int cx, int cy, int px, int py, unsigned int col)
{
    cnv_set(buf, cw, ch, cx + px, cy + py, col);
    cnv_set(buf, cw, ch, cx - px, cy + py, col);
    cnv_set(buf, cw, ch, cx + px, cy - py, col);
    cnv_set(buf, cw, ch, cx - px, cy - py, col);
    cnv_set(buf, cw, ch, cx + py, cy + px, col);
    cnv_set(buf, cw, ch, cx - py, cy + px, col);
    cnv_set(buf, cw, ch, cx + py, cy - px, col);
    cnv_set(buf, cw, ch, cx - py, cy - px, col);
}

void cnv_draw_circle(unsigned int *buf, int cw, int ch,
                     int cx, int cy, int r, unsigned int col)
{
    int px = 0, py = r;
    int d = 3 - 2 * r;
    if (r <= 0) return;
    while (py >= px) {
        plot8_cnv(buf, cw, ch, cx, cy, px, py, col);
        px++;
        if (d > 0) { py--; d += 4 * (px - py) + 10; }
        else         d += 4 * px + 6;
    }
}

/* =========================================================================
 * Text rendering (IBM VGA 8×8 bitmap font)
 * ========================================================================= */

void cnv_draw_char(unsigned int *buf, int cw, int ch,
                   int x, int y, char c,
                   unsigned int fg, unsigned int bg)
{
    unsigned int idx = (unsigned char)c;
    if (idx >= 128u) idx = 0u;

    for (int row = 0; row < CNV_FONT_H; row++) {
        unsigned char bits = (unsigned char)font8x8_basic[idx][row];
        for (int col2 = 0; col2 < CNV_FONT_W; col2++) {
            if (bits & (1u << col2)) {
                cnv_set(buf, cw, ch, x + col2, y + row, fg);
            } else if (bg != COLOR_TRANSPARENT) {
                cnv_set(buf, cw, ch, x + col2, y + row, bg);
            }
        }
    }
}

void cnv_draw_str(unsigned int *buf, int cw, int ch,
                  int x, int y, const char *s,
                  unsigned int fg, unsigned int bg)
{
    int x_origin = x;
    if (!s) return;
    while (*s) {
        char c = *s++;
        if (c == '\n') { y += CNV_FONT_LINE_H; x = x_origin; }
        else if (c == '\r') { x = x_origin; }
        else if (c == '\b') { x -= CNV_FONT_W; if (x < x_origin) x = x_origin; }
        else { cnv_draw_char(buf, cw, ch, x, y, c, fg, bg); x += CNV_FONT_W; }
    }
}

/* =========================================================================
 * Composite widgets
 * ========================================================================= */

void cnv_button(unsigned int *buf, int cw, int ch,
                int x, int y, int w, int h,
                const char *label,
                unsigned int fg, unsigned int bg, unsigned int border)
{
    /* Background: vertical gradient (bg → slightly darker) */
    unsigned int bg_dark = COLOR_RGB(
        (int)COLOR_R(bg) * 3 / 4,
        (int)COLOR_G(bg) * 3 / 4,
        (int)COLOR_B(bg) * 3 / 4);
    cnv_grad_v(buf, cw, ch, x, y, w, h, bg, bg_dark);

    /* 1-px border */
    cnv_draw_round_rect(buf, cw, ch, x, y, w, h, 4, border);

    /* Centred label */
    if (label) {
        int tw = 0;
        const char *p = label;
        while (*p++) tw += CNV_FONT_W;
        int tx = x + (w - tw) / 2;
        int ty = y + (h - CNV_FONT_H) / 2;
        cnv_draw_str(buf, cw, ch, tx, ty, label, fg, COLOR_TRANSPARENT);
    }
}

void cnv_label(unsigned int *buf, int cw, int ch,
               int x, int y, const char *text,
               unsigned int fg, unsigned int bg)
{
    if (!text) return;
    if (bg != COLOR_TRANSPARENT) {
        /* Measure text width for background rect */
        int tw = 0;
        const char *p = text;
        while (*p++) tw += CNV_FONT_W;
        cnv_fill_rect(buf, cw, ch, x - 1, y - 1, tw + 2, CNV_FONT_H + 2, bg);
    }
    cnv_draw_str(buf, cw, ch, x, y, text, fg, COLOR_TRANSPARENT);
}

void cnv_hsep(unsigned int *buf, int cw, int ch,
              int x, int y, int w, unsigned int col)
{
    cnv_fill_rect(buf, cw, ch, x, y, w, 1, col);
}

void cnv_progress(unsigned int *buf, int cw, int ch,
                  int x, int y, int w, int h,
                  int value, int max_val,
                  unsigned int fill_col, unsigned int bg_col, unsigned int border_col)
{
    /* Background */
    cnv_fill_rect(buf, cw, ch, x, y, w, h, bg_col);
    /* Fill portion */
    if (max_val > 0 && value > 0) {
        int fw = (w - 2) * value / max_val;
        if (fw < 0) fw = 0;
        if (fw > w - 2) fw = w - 2;
        cnv_fill_rect(buf, cw, ch, x + 1, y + 1, fw, h - 2, fill_col);
    }
    /* Border */
    cnv_draw_rect(buf, cw, ch, x, y, w, h, border_col);
}
