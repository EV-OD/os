/* =========================================================================
 * gui/gui_term.c – Framebuffer terminal (implements terminal_t)
 *
 * Creates a scrolling text terminal inside a WM window.
 * The get_char() vtable entry drives the compositor while waiting for
 * keyboard input – this is how the single-threaded OS stays interactive.
 * ========================================================================= */

#include "gui/gui_term.h"
#include "gui/fb.h"
#include "gui/gfx.h"
#include "gui/font.h"
#include "gui/color.h"
#include "gui/wm.h"
#include "gui/desktop.h"
#include "gui/mouse.h"
#include "terminal.h"
#include "keyboard.h"
#include "kheap.h"
#include "string.h"
#include "log.h"

/* -------------------------------------------------------------------------
 * Internal structures
 * ------------------------------------------------------------------------- */

#define GT_FG  COLOR_RGB(0xCC, 0xFF, 0xCC)   /* Matrix-green foreground */
#define GT_BG  COLOR_RGB(0x00, 0x08, 0x00)   /* Near-black background   */

typedef struct {
    /* Back-reference to the WM window */
    wm_window_t *win;

    /* Character grid */
    int cols, rows;
    char    *cells_char;    /* cols × rows  */
    color_t *cells_fg;
    color_t *cells_bg;

    /* Cursor position */
    int cur_col, cur_row;

    /* vtable (returned to caller as terminal_t *) */
    terminal_t term;

    /* Cursor blink */
    int cursor_visible;
    unsigned int blink_tick;
} gui_term_state_t;

/* -------------------------------------------------------------------------
 * Forward declarations
 * ------------------------------------------------------------------------- */

static void gt_paint(wm_window_t *win);
static void gt_put_char(char c);
static void gt_put_char_color(char c, unsigned char vga_fg);
static void gt_put_string(const char *s);
static void gt_put_string_color(const char *s, unsigned char vga_fg);
static int  gt_get_char(void);
static int  gt_read_line(char *buf, unsigned int max);
static void gt_clear(void);
static int  gt_tprintf(const char *fmt, ...);

/* Active terminal state (single terminal per session) */
static gui_term_state_t *s_gt = (void *)0;

/* -------------------------------------------------------------------------
 * Grid helpers
 * ------------------------------------------------------------------------- */

static inline char *cell_char(gui_term_state_t *gt, int col, int row)
{
    return &gt->cells_char[row * gt->cols + col];
}

static inline color_t *cell_fg(gui_term_state_t *gt, int col, int row)
{
    return &gt->cells_fg[row * gt->cols + col];
}

static inline color_t *cell_bg(gui_term_state_t *gt, int col, int row)
{
    return &gt->cells_bg[row * gt->cols + col];
}

static void gt_scroll_up(gui_term_state_t *gt)
{
    int row, col;
    /* Shift rows 1..rows-1 up by one */
    for (row = 0; row < gt->rows - 1; row++) {
        for (col = 0; col < gt->cols; col++) {
            *cell_char(gt, col, row) = *cell_char(gt, col, row + 1);
            *cell_fg  (gt, col, row) = *cell_fg  (gt, col, row + 1);
            *cell_bg  (gt, col, row) = *cell_bg  (gt, col, row + 1);
        }
    }
    /* Clear last row */
    for (col = 0; col < gt->cols; col++) {
        *cell_char(gt, col, gt->rows - 1) = ' ';
        *cell_fg  (gt, col, gt->rows - 1) = GT_FG;
        *cell_bg  (gt, col, gt->rows - 1) = GT_BG;
    }
}

static void gt_advance_cursor(gui_term_state_t *gt)
{
    gt->cur_col++;
    if (gt->cur_col >= gt->cols) {
        gt->cur_col = 0;
        gt->cur_row++;
    }
    if (gt->cur_row >= gt->rows) {
        gt_scroll_up(gt);
        gt->cur_row = gt->rows - 1;
    }
}

static void gt_emit(gui_term_state_t *gt, char c, color_t fg, color_t bg)
{
    switch (c) {
    case '\n':
        gt->cur_col = 0;
        gt->cur_row++;
        if (gt->cur_row >= gt->rows) {
            gt_scroll_up(gt);
            gt->cur_row = gt->rows - 1;
        }
        return;
    case '\r':
        gt->cur_col = 0;
        return;
    case '\b':
        if (gt->cur_col > 0) {
            gt->cur_col--;
            *cell_char(gt, gt->cur_col, gt->cur_row) = ' ';
            *cell_fg  (gt, gt->cur_col, gt->cur_row) = fg;
            *cell_bg  (gt, gt->cur_col, gt->cur_row) = bg;
        }
        return;
    case '\t':
        {
            int next = ((gt->cur_col / 8) + 1) * 8;
            while (gt->cur_col < next) {
                *cell_char(gt, gt->cur_col, gt->cur_row) = ' ';
                *cell_fg  (gt, gt->cur_col, gt->cur_row) = fg;
                *cell_bg  (gt, gt->cur_col, gt->cur_row) = bg;
                gt_advance_cursor(gt);
            }
        }
        return;
    default:
        *cell_char(gt, gt->cur_col, gt->cur_row) = c;
        *cell_fg  (gt, gt->cur_col, gt->cur_row) = fg;
        *cell_bg  (gt, gt->cur_col, gt->cur_row) = bg;
        gt_advance_cursor(gt);
        return;
    }
}

/* -------------------------------------------------------------------------
 * Paint callback (called by WM when window is dirty)
 * ------------------------------------------------------------------------- */

static void gt_paint(wm_window_t *win)
{
    gui_term_state_t *gt = (gui_term_state_t *)win->canvas; /* NOT the canvas! */
    (void)gt;
    /* Actual rendering done directly into win->canvas in gt_render_grid */
}

/* Render the full grid into the window's canvas. */
static void gt_render_grid(gui_term_state_t *gt)
{
    int row, col;
    int cw = gt->win->w;
    int ch = gt->win->h - TITLE_BAR_H;

    /* Black-fill the entire canvas */
    {
        int total = cw * ch;
        for (int i = 0; i < total; i++)
            gt->win->canvas[i] = GT_BG;
    }

    for (row = 0; row < gt->rows; row++) {
        for (col = 0; col < gt->cols; col++) {
            char  c  = *cell_char(gt, col, row);
            color_t fg = *cell_fg(gt, col, row);
            color_t bg = *cell_bg(gt, col, row);
            int px = col * FONT_W;
            int py = row * FONT_H;

            /* Draw background cell into canvas */
            for (int dy = 0; dy < FONT_H && py + dy < ch; dy++)
                for (int dx = 0; dx < FONT_W && px + dx < cw; dx++)
                    gt->win->canvas[(py + dy) * cw + (px + dx)] = bg;

            /* Draw glyph into canvas */
            {
                unsigned int idx = (unsigned char)c < 128 ? (unsigned char)c : 0;
                extern char font8x8_basic[128][8];
                for (int dy = 0; dy < FONT_H && py + dy < ch; dy++) {
                    unsigned char bits = (unsigned char)font8x8_basic[idx][dy];
                    for (int dx = 0; dx < FONT_W && px + dx < cw; dx++) {
                        if (bits & (0x80u >> dx))
                            gt->win->canvas[(py + dy) * cw + (px + dx)] = fg;
                    }
                }
            }
        }
    }

    /* Cursor block (simple invert) */
    {
        int px = gt->cur_col * FONT_W;
        int py = gt->cur_row * FONT_H;
        for (int dy = 0; dy < FONT_H && py + dy < ch; dy++)
            for (int dx = 0; dx < FONT_W && px + dx < cw; dx++)
                gt->win->canvas[(py + dy) * cw + (px + dx)] ^= 0x00FFFFFF;
    }
}

/* -------------------------------------------------------------------------
 * vtable implementations
 * ------------------------------------------------------------------------- */

static void gt_put_char(char c)
{
    if (!s_gt) return;
    gt_emit(s_gt, c, GT_FG, GT_BG);
    gt_render_grid(s_gt);
    s_gt->win->dirty = 1;
}

static void gt_put_char_color(char c, unsigned char vga_fg)
{
    if (!s_gt) return;
    gt_emit(s_gt, c, color_from_vga(vga_fg & 0x0F), GT_BG);
    gt_render_grid(s_gt);
    s_gt->win->dirty = 1;
}

static void gt_put_string(const char *s)
{
    if (!s_gt || !s) return;
    while (*s) gt_emit(s_gt, *s++, GT_FG, GT_BG);
    gt_render_grid(s_gt);
    s_gt->win->dirty = 1;
}

static void gt_put_string_color(const char *s, unsigned char vga_fg)
{
    color_t fg = color_from_vga(vga_fg & 0x0F);
    if (!s_gt || !s) return;
    while (*s) gt_emit(s_gt, *s++, fg, GT_BG);
    gt_render_grid(s_gt);
    s_gt->win->dirty = 1;
}

/**
 * Blocking get_char that drives the compositor while waiting.
 * This is the heart of the cooperative single-threaded GUI.
 */
static int gt_get_char(void)
{
    int c;
    while ((c = keyboard_read_char()) == -1) {
        /* Keep the screen alive while the shell is blocked */
        wm_paint_all();
        desktop_draw_taskbar();
        mouse_draw_cursor();
        fb_flush();
    }
    return c;
}

static int gt_read_line(char *buf, unsigned int max)
{
    unsigned int pos = 0;
    int c;

    if (!buf || max == 0) return -1;

    while (pos < max - 1) {
        c = gt_get_char();
        if (c == '\n' || c == '\r') {
            gt_put_char('\n');
            break;
        }
        if (c == '\b' || c == 127) {
            if (pos > 0) {
                pos--;
                gt_put_char('\b');
            }
            continue;
        }
        buf[pos++] = (char)c;
        gt_put_char((char)c);
    }
    buf[pos] = '\0';
    return (int)pos;
}

static void gt_clear(void)
{
    int row, col;
    if (!s_gt) return;
    for (row = 0; row < s_gt->rows; row++)
        for (col = 0; col < s_gt->cols; col++) {
            *cell_char(s_gt, col, row) = ' ';
            *cell_fg  (s_gt, col, row) = GT_FG;
            *cell_bg  (s_gt, col, row) = GT_BG;
        }
    s_gt->cur_col = s_gt->cur_row = 0;
    gt_render_grid(s_gt);
    s_gt->win->dirty = 1;
}

static int gt_tprintf(const char *fmt, ...)
{
    char buf[512];
    int n;
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    n = vsprintf(buf, fmt, args);
    __builtin_va_end(args);
    if (n > 0) gt_put_string(buf);
    return n;
}

/* -------------------------------------------------------------------------
 * gui_term_create
 * ------------------------------------------------------------------------- */

terminal_t *gui_term_create(wm_window_t *win)
{
    gui_term_state_t *gt;
    int client_w, client_h, cols, rows, ncells;

    if (!win) return (void *)0;

    client_w = win->w;
    client_h = win->h - TITLE_BAR_H;
    cols = client_w / FONT_W;
    rows = client_h / FONT_H;

    if (cols <= 0 || rows <= 0) return (void *)0;

    gt = (gui_term_state_t *)kmalloc(sizeof(gui_term_state_t));
    if (!gt) return (void *)0;

    ncells = cols * rows;
    gt->cells_char = (char    *)kmalloc((unsigned int)ncells);
    gt->cells_fg   = (color_t *)kmalloc((unsigned int)ncells * 4u);
    gt->cells_bg   = (color_t *)kmalloc((unsigned int)ncells * 4u);

    if (!gt->cells_char || !gt->cells_fg || !gt->cells_bg) {
        if (gt->cells_char) kfree(gt->cells_char);
        if (gt->cells_fg)   kfree(gt->cells_fg);
        if (gt->cells_bg)   kfree(gt->cells_bg);
        kfree(gt);
        return (void *)0;
    }

    gt->win     = win;
    gt->cols    = cols;
    gt->rows    = rows;
    gt->cur_col = gt->cur_row = 0;
    gt->cursor_visible = 1;
    gt->blink_tick     = 0;

    /* Initialise grid with spaces */
    for (int i = 0; i < ncells; i++) {
        gt->cells_char[i] = ' ';
        gt->cells_fg  [i] = GT_FG;
        gt->cells_bg  [i] = GT_BG;
    }

    /* Wire vtable */
    gt->term.put_char        = gt_put_char;
    gt->term.put_char_color  = gt_put_char_color;
    gt->term.put_string      = gt_put_string;
    gt->term.put_string_color= gt_put_string_color;
    gt->term.get_char        = gt_get_char;
    gt->term.read_line       = gt_read_line;
    gt->term.clear           = gt_clear;
    gt->term.tprintf         = gt_tprintf;

    /* Set WM paint callback */
    win->on_paint = gt_paint;

    /* Register as active global state */
    s_gt = gt;

    gt_render_grid(gt);
    win->dirty = 1;

    log_info("[gui_term] %dx%d terminal in window '%s'",
             cols, rows, win->title);

    return &gt->term;
}

/* -------------------------------------------------------------------------
 * gui_term_destroy
 * ------------------------------------------------------------------------- */

void gui_term_destroy(terminal_t *t)
{
    gui_term_state_t *gt;
    if (!t) return;

    gt = (gui_term_state_t *)((char *)t - __builtin_offsetof(gui_term_state_t, term));

    if (s_gt == gt) s_gt = (void *)0;

    kfree(gt->cells_char);
    kfree(gt->cells_fg);
    kfree(gt->cells_bg);
    kfree(gt);
}

/* -------------------------------------------------------------------------
 * gui_term_push_char
 * ------------------------------------------------------------------------- */

void gui_term_push_char(terminal_t *t, char c)
{
    (void)t;
    /* In this implementation keyboard input comes directly from keyboard.h
     * ring buffer; push_char is a no-op. */
    (void)c;
}
