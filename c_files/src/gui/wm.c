/* =========================================================================
 * gui/wm.c – Lightweight software window manager
 *
 * Maintains a fixed-size window stack (front→back Z-order).
 * wm_stack[0] is always the topmost (focused) window.
 * Compositing iterates back→front so the focused window is drawn last
 * and appears on top.
 * ========================================================================= */

#include "gui/wm.h"
#include "gui/fb.h"
#include "gui/gfx.h"
#include "gui/canvas.h"
#include "gui/font.h"
#include "gui/color.h"
#include "kheap.h"
#include "string.h"
#include "log.h"
#include "process.h"
#include "sched.h"

/* -------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------- */

static wm_window_t *wm_stack[WM_MAX_WINDOWS];
static int          wm_count = 0;

/* Drag state */
static wm_window_t *drag_win    = (void *)0;
static int          drag_off_x  = 0;
static int          drag_off_y  = 0;
static int          prev_lbtn   = 0;    /* left-button state from last frame */

/* -------------------------------------------------------------------------
 * wm_init
 * ------------------------------------------------------------------------- */

void wm_init(void)
{
    wm_count = 0;
    drag_win = (void *)0;
    prev_lbtn = 0;
    log_info("[wm] window manager initialised (max %d windows)", WM_MAX_WINDOWS);
}

/* -------------------------------------------------------------------------
 * wm_create
 * ------------------------------------------------------------------------- */

wm_window_t *wm_create(int x, int y, int w, int h, const char *title)
{
    wm_window_t *win;
    int i;

    if (wm_count >= WM_MAX_WINDOWS) {
        log_warning("[wm] wm_create: window stack full");
        return (void *)0;
    }
    if (w <= 0 || h <= TITLE_BAR_H) {
        log_warning("[wm] wm_create: invalid size %dx%d", w, h);
        return (void *)0;
    }

    win = (wm_window_t *)kmalloc(sizeof(wm_window_t));
    if (!win) {
        log_error("[wm] wm_create: kmalloc failed for descriptor");
        return (void *)0;
    }

    /* No canvas yet – allocated lazily by wm_alloc_canvas() */
    win->canvas        = (void *)0;
    win->front_canvas  = (void *)0;
    win->canvas_size   = 0;

    win->x = x; win->y = y;
    win->w = w; win->h = h;

    /* Copy title (max 63 chars) */
    {
        int tlen = 0;
        while (title && title[tlen] && tlen < 63) tlen++;
        for (i = 0; i < tlen; i++) win->title[i] = title[i];
        win->title[tlen] = '\0';
    }

    win->visible    = 1;
    win->dirty      = 1;
    win->on_paint   = (void *)0;
    win->on_key     = (void *)0;
    win->on_mouse   = (void *)0;
    win->_stack_idx = 0;
    win->pen_color  = 0xFFFFFF;  /* default stroke = white */
    win->bg_color   = 0x001020;  /* default bg = dark navy  */
    win->owner_pid  = 0;

    /* Per-window key queue – must be zero so head==tail (empty) on first use */
    win->kq_head = 0;
    win->kq_tail = 0;

    /* Push to front of Z-stack (shift others back by one) */
    for (i = wm_count; i > 0; i--)
        wm_stack[i] = wm_stack[i - 1];
    wm_stack[0] = win;
    wm_count++;

    /* Update _stack_idx for all windows */
    for (i = 0; i < wm_count; i++)
        wm_stack[i]->_stack_idx = i;

    log_info("[wm] created '%s' (%dx%d at %d,%d)", win->title, w, h, x, y);
    return win;
}

/* -------------------------------------------------------------------------
 * wm_alloc_canvas
 * ------------------------------------------------------------------------- */

int wm_alloc_canvas(wm_window_t *win)
{
    if (!win) return 0;
    if (win->canvas) return 1;  /* already allocated */

    int cw = win->w;
    int ch = win->h - TITLE_BAR_H;
    if (cw <= 0 || ch <= 0) return 0;

    unsigned int sz = (unsigned int)(cw * ch) * sizeof(unsigned int);

    win->canvas = (unsigned int *)kmalloc(sz);
    if (!win->canvas) {
        log_error("[wm] wm_alloc_canvas: draw kmalloc failed (%dx%d)", cw, ch);
        return 0;
    }

    win->front_canvas = (unsigned int *)kmalloc(sz);
    if (!win->front_canvas) {
        kfree(win->canvas);
        win->canvas = (void *)0;
        log_error("[wm] wm_alloc_canvas: front kmalloc failed (%dx%d)", cw, ch);
        return 0;
    }

    win->canvas_size = (unsigned int)(cw * ch);

    /* Fill both buffers with the window's background colour. */
    cnv_clear(win->canvas,       cw, ch, win->bg_color);
    cnv_clear(win->front_canvas, cw, ch, win->bg_color);

    log_info("[wm] canvas allocated for '%s' (%dx%d, %d bytes)",
             win->title, cw, ch, cw * ch * 4);
    return 1;
}

/* -------------------------------------------------------------------------
 * wm_present_canvas – copy draw canvas → front canvas (called by gui_flush).
 *
 * This is the double-buffer swap point.  The compositor always blits
 * front_canvas; the user process always draws into canvas.  Copying here
 * ensures the compositor never sees a partial frame (half-cleared canvas).
 * ------------------------------------------------------------------------- */
void wm_present_canvas(wm_window_t *win)
{
    if (!win || !win->canvas || !win->front_canvas) return;
    unsigned int  n   = win->canvas_size;
    unsigned int *dst = win->front_canvas;
    unsigned int *src = win->canvas;
    /* Use a simple word loop – compiler will auto-vectorise or use rep movsd */
    while (n--) *dst++ = *src++;
}

/* -------------------------------------------------------------------------
 * wm_destroy
 * ------------------------------------------------------------------------- */

void wm_destroy(wm_window_t *win)
{
    int i, found = -1;

    if (!win) return;

    /* Find position in stack */
    for (i = 0; i < wm_count; i++) {
        if (wm_stack[i] == win) { found = i; break; }
    }
    if (found < 0) return;

    /* Free both canvas buffers and the descriptor */
    if (win->canvas)       kfree(win->canvas);
    if (win->front_canvas) kfree(win->front_canvas);
    kfree(win);

    /* Compact the stack */
    for (i = found; i < wm_count - 1; i++) {
        wm_stack[i] = wm_stack[i + 1];
        wm_stack[i]->_stack_idx = i;
    }
    wm_stack[wm_count - 1] = (void *)0;
    wm_count--;

    if (drag_win == win) drag_win = (void *)0;
}

/* -------------------------------------------------------------------------
 * wm_raise
 * ------------------------------------------------------------------------- */

void wm_raise(wm_window_t *win)
{
    int i, found = -1;
    if (!win) return;

    for (i = 0; i < wm_count; i++) {
        if (wm_stack[i] == win) { found = i; break; }
    }
    if (found <= 0) return;   /* already at top (or not found) */

    for (i = found; i > 0; i--)
        wm_stack[i] = wm_stack[i - 1];
    wm_stack[0] = win;

    for (i = 0; i < wm_count; i++)
        wm_stack[i]->_stack_idx = i;
}

/* -------------------------------------------------------------------------
 * wm_focused
 * ------------------------------------------------------------------------- */

wm_window_t *wm_focused(void)
{
    return (wm_count > 0) ? wm_stack[0] : (void *)0;
}

/* -------------------------------------------------------------------------
 * wm_paint (single window → back-buffer)
 * ------------------------------------------------------------------------- */

void wm_paint(wm_window_t *win)
{
    int is_focused = (wm_stack[0] == win);
    color_t title_bg = is_focused ? COLOR_WINDOW_TITLE : COLOR_DKGRAY;
    int client_h = win->h - TITLE_BAR_H;

    if (!win->visible) return;

    /* ---- Call on_paint if dirty ---- */
    if (win->dirty && win->on_paint) {
        win->on_paint(win);
        win->dirty = 0;
    }

    /* ---- Title bar ---- */
    gfx_fill_gradient_v(win->x, win->y, win->w, TITLE_BAR_H,
                        title_bg,
                        COLOR_RGB(COLOR_R(title_bg) / 2,
                                  COLOR_G(title_bg) / 2,
                                  COLOR_B(title_bg) / 2));

    /* Title text */
    {
        int tx = win->x + 6;
        int ty = win->y + (TITLE_BAR_H - FONT_H) / 2;
        font_draw_str(tx, ty, win->title, COLOR_WHITE, COLOR_TRANSPARENT);
    }

    /* Close button (red square, top-right) */
    {
        int bx = win->x + win->w - 18;
        int by = win->y + (TITLE_BAR_H - 14) / 2;
        gfx_fill_rect(bx, by, 14, 14, COLOR_CLOSE_BTN);
        /* × symbol */
        gfx_draw_line(bx + 3, by + 3, bx + 10, by + 10, COLOR_WHITE);
        gfx_draw_line(bx + 10, by + 3, bx + 3, by + 10, COLOR_WHITE);
    }

    /* ---- Canvas blit (user windows) ---- */
    /* Always blit from front_canvas (complete frame written by gui_flush),
     * never from canvas (the draw buffer which may be mid-clear). */
    if (win->front_canvas && client_h > 0) {
        gfx_blit(win->x, win->y + TITLE_BAR_H,
                 win->w, client_h,
                 win->front_canvas, win->w);
    } else if (win->canvas && !win->front_canvas && client_h > 0) {
        /* Fallback for windows without double-buffering (e.g. gui_term). */
        gfx_blit(win->x, win->y + TITLE_BAR_H,
                 win->w, client_h,
                 win->canvas, win->w);
    }
    /* Windows with canvas == NULL (e.g. gui_term) fill their own client
     * area via the on_paint callback called above – no fill needed here. */

    /* ---- Border ---- */
    gfx_draw_rect(win->x, win->y, win->w, win->h, COLOR_WINDOW_BORDER);
}

/* -------------------------------------------------------------------------
 * wm_paint_all (composite back→front)
 * ------------------------------------------------------------------------- */

void wm_paint_all(void)
{
    int i;
    for (i = wm_count - 1; i >= 0; i--)
        if (wm_stack[i] && wm_stack[i]->visible)
            wm_paint(wm_stack[i]);
}

/* -------------------------------------------------------------------------
 * wm_invalidate_all
 * ------------------------------------------------------------------------- */

void wm_invalidate_all(void)
{
    int i;
    for (i = 0; i < wm_count; i++)
        if (wm_stack[i]) wm_stack[i]->dirty = 1;
}

/* -------------------------------------------------------------------------
 * wm_dispatch_key
 * ------------------------------------------------------------------------- */

void wm_window_key_push(wm_window_t *win, unsigned char c)
{
    if (!win) return;
    unsigned int next = (win->kq_head + 1) % WM_KEY_QUEUE_SIZE;
    if (next == win->kq_tail) return; /* queue full, drop */
    win->kq_buf[win->kq_head] = c;
    win->kq_head = next;
}

int wm_window_key_pop(wm_window_t *win)
{
    if (!win || win->kq_head == win->kq_tail) return -1;
    unsigned char c = win->kq_buf[win->kq_tail];
    win->kq_tail = (win->kq_tail + 1) % WM_KEY_QUEUE_SIZE;
    return (int)c;
}

int wm_window_key_available(wm_window_t *win)
{
    if (!win) return 0;
    return win->kq_head != win->kq_tail;
}

void wm_dispatch_key(char c)
{
    wm_window_t *f = wm_focused();

    /* Ctrl+C (0x03) – kill the focused window's owner process. */
    if (c == 3) {
        if (f && f->owner_pid > 0) {
            process_t *p = process_find((unsigned int)f->owner_pid);
            if (p && p->state != PROC_DEAD) {
                p->killed = 1;
                sched_wake_process(p);
            }
        }
        return;
    }

    if (f && f->on_key)
        f->on_key(f, c);
    else if (f)
        /* User-process window (no on_key): deposit into per-window queue
         * so sys_gui_wait / sys_gui_poll can drain it. */
        wm_window_key_push(f, (unsigned char)c);
}

/* -------------------------------------------------------------------------
 * Hit-test helpers
 * ------------------------------------------------------------------------- */

/* Returns 1 if (mx,my) is on the close button of win. */
static int hit_close(wm_window_t *win, int mx, int my)
{
    int bx = win->x + win->w - 18;
    int by = win->y + (TITLE_BAR_H - 14) / 2;
    return (mx >= bx && mx < bx + 14 && my >= by && my < by + 14);
}

/* Returns 1 if (mx,my) is in the title bar (but not close button). */
static int hit_titlebar(wm_window_t *win, int mx, int my)
{
    return (mx >= win->x && mx < win->x + win->w - 18 &&
            my >= win->y && my < win->y + TITLE_BAR_H);
}

/* Returns the topmost visible window that contains (mx,my), or NULL. */
static wm_window_t *hit_test(int mx, int my)
{
    int i;
    for (i = 0; i < wm_count; i++) {
        wm_window_t *w = wm_stack[i];
        if (w && w->visible &&
            mx >= w->x && mx < w->x + w->w &&
            my >= w->y && my < w->y + w->h)
            return w;
    }
    return (void *)0;
}

/* -------------------------------------------------------------------------
 * wm_dispatch_mouse
 * ------------------------------------------------------------------------- */

void wm_dispatch_mouse(int mx, int my, unsigned char btns)
{
    int lbtn = btns & 0x01;
    wm_window_t *hit;

    /* --- Drag continuation --- */
    if (drag_win && lbtn) {
        drag_win->x = mx - drag_off_x;
        drag_win->y = my - drag_off_y;
        /* keep window title bar on screen */
        if (drag_win->x < 0) drag_win->x = 0;
        if (drag_win->y < 0) drag_win->y = 0;
        {
            int max_x = (int)fb_width()  - drag_win->w;
            int max_y = (int)fb_height() - TITLE_BAR_H;
            if (drag_win->x > max_x) drag_win->x = max_x;
            if (drag_win->y > max_y) drag_win->y = max_y;
        }
        wm_invalidate_all();
        prev_lbtn = lbtn;
        return;
    }
    if (!lbtn) drag_win = (void *)0;

    /* --- New click --- */
    if (lbtn && !prev_lbtn) {
        hit = hit_test(mx, my);
        if (hit) {
            wm_raise(hit);
            wm_invalidate_all();

            /* Close button? */
            if (hit_close(hit, mx, my)) {
                /* Save owner pid before destroy invalidates the pointer. */
                int owner = hit->owner_pid;
                wm_destroy(hit);
                wm_invalidate_all();  /* repaint immediately – removes residue */
                /* Signal the owning process to terminate. */
                if (owner > 0) {
                    process_t *p = process_find((unsigned int)owner);
                    if (p && p->state != PROC_DEAD) {
                        p->killed = 1;  /* process exits cleanly via syscall loop */  /* process exits cleanly via syscall loop */
                        sched_wake_process(p); /* unblock it if it is sleeping */
                    }
                }
                prev_lbtn = lbtn;
                return;
            }

            /* Title bar drag? */
            if (hit_titlebar(hit, mx, my) && !hit->no_drag) {
                drag_win   = hit;
                drag_off_x = mx - hit->x;
                drag_off_y = my - hit->y;
                prev_lbtn  = lbtn;
                return;
            }

            /* Pass to window's on_mouse with client-relative coords */
            if (hit->on_mouse) {
                int rx = mx - hit->x;
                int ry = my - (hit->y + TITLE_BAR_H);
                hit->on_mouse(hit, rx, ry, btns);
            }
        }
    } else if (!lbtn && prev_lbtn) {
        /* Button release – forward to focused window */
        hit = wm_focused();
        if (hit && hit->on_mouse) {
            int rx = mx - hit->x;
            int ry = my - (hit->y + TITLE_BAR_H);
            hit->on_mouse(hit, rx, ry, btns);
        }
    }

    prev_lbtn = lbtn;
}

/* -------------------------------------------------------------------------
 * wm_destroy_by_pid – destroy all windows owned by a process.
 *
 * Called from the scheduler when a user process dies so orphaned windows
 * are cleaned up automatically.  Iterates backwards because wm_destroy
 * compacts the wm_stack array.
 * ------------------------------------------------------------------------- */

void wm_destroy_by_pid(int pid)
{
    int i, found = 0;

    if (pid <= 0) return;

    for (i = wm_count - 1; i >= 0; i--) {
        if (wm_stack[i] && wm_stack[i]->owner_pid == pid) {
            log_info("[wm] auto-destroying orphaned window '%s' (owner pid=%d)",
                     wm_stack[i]->title, pid);
            wm_destroy(wm_stack[i]);
            found = 1;
        }
    }

    if (found) {
        wm_invalidate_all();
        wm_paint_all();
        fb_flush();
    }
}
