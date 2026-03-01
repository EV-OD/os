/* =========================================================================
 * gui/desktop.c – Desktop environment (wallpaper, taskbar, event loop)
 *
 * desktop_run() spins forever:
 *   input → wm dispatch → wm_paint_all → taskbar → cursor → fb_flush
 *
 * Key events are forwarded to the focused WM window.
 * The GUI terminal window is created at startup; the shell runs inside it
 * (gui_term's get_char does the paint loop while waiting for input).
 * ========================================================================= */

#include "gui/desktop.h"
#include "gui/fb.h"
#include "gui/gfx.h"
#include "gui/font.h"
#include "gui/color.h"
#include "gui/mouse.h"
#include "gui/wm.h"
#include "gui/gui_term.h"
#include "keyboard.h"
#include "log.h"
#include "string.h"
#include "pit.h"
#include "process.h"
#include "sched.h"
#include "terminal.h"
#include "shell.h"
#include "rox.h"   /* pit_get_ticks() – frame rate limiter */
#include "vfs.h"

/* Target frame period: 2 ticks × 10 ms = ~50 fps */
#define FRAME_TICKS  2u

/* Double-click threshold: 50 ticks = 500 ms */
#define DBLCLICK_TICKS 50u

/* -------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------- */

#define MAX_ICONS  16

typedef struct {
    int  x, y;
    char label_buf[32];          /* owned copy of the label */
    const char *label;           /* points into label_buf   */
    char path[256];              /* .rox path; empty for built-in callbacks */
    void (*on_click)(void);      /* built-in callback, or NULL for path icons */
    int  used;
} icon_slot_t;

static icon_slot_t s_icons[MAX_ICONS];
static int         s_icon_count = 0;

/* Double-click tracking */
static int          s_last_click_icon = -1;
static unsigned int s_last_click_tick = 0;

/* Trampoline state for path-based icon launch (one at a time) */
static char s_icon_launch_path[256];

static void icon_rox_task(void)
{
    char lpath[256];
    int  i;
    for (i = 0; i < 255; i++) lpath[i] = s_icon_launch_path[i];
    lpath[255] = '\0';
    rox_load_and_run(lpath, 0, (void *)0);
}

/* -------------------------------------------------------------------------
 * Terminal spawn state
 * A pending terminal_t* is stored here so the kernel shell task can pick
 * it up without needing a closure.  Only one spawn may be in-flight at a
 * time; the flag is cleared by gui_shell_task() on entry.
 * ------------------------------------------------------------------------- */
static terminal_t   *s_spawn_pending_term = (void *)0;
static int           s_spawn_term_offset  = 0;   /* cascade new windows */

static void gui_shell_task(void)
{
    terminal_t *t = s_spawn_pending_term;
    s_spawn_pending_term = (void *)0;
    if (t) term_set_active(t);
    shell_run();
}

static void rxt_launch_task(void)
{
    rox_load_and_run("/bin/rxt.rox", 0, (void *)0);
}

void desktop_spawn_terminal(void)
{
    if (s_spawn_pending_term) return;  /* already one pending */

    int sw = (int)fb_width();
    int sh = (int)fb_height();
    int step = (s_spawn_term_offset % 5) * 24;
    s_spawn_term_offset++;

    int wx = 4 + step;
    int wy = 4 + step;
    int ww = sw - wx - 8 - step;
    int wh = sh - TASKBAR_H - wy - 8 - step;
    if (ww < 400) ww = 400;
    if (wh < 300) wh = 300;

    wm_window_t *win = wm_create(wx, wy, ww, wh, "Terminal");
    if (!win) return;

    terminal_t *t = gui_term_create(win);
    if (!t) { wm_destroy(win); return; }

    s_spawn_pending_term = t;
    process_t *p = process_create("shell", gui_shell_task, 5);
    if (p) {
        sched_add(p);
    } else {
        s_spawn_pending_term = (void *)0;
    }
}

void desktop_open_rxt(void)
{
    process_t *p = process_create("rxt-icon", rxt_launch_task, 5);
    if (p) sched_add(p);
}

/* -------------------------------------------------------------------------
 * desktop_draw_wallpaper
 * ------------------------------------------------------------------------- */

void desktop_draw_wallpaper(void)
{
    int w = (int)fb_width();
    int h = (int)fb_height() - TASKBAR_H;

    /* Vertical gradient: dark navy → slightly lighter */
    gfx_fill_gradient_v(0, 0, w, h,
                        COLOR_RGB(0x1A, 0x2A, 0x4A),
                        COLOR_RGB(0x0A, 0x18, 0x30));

    /* OS name watermark in the lower-right corner */
    {
        const char *name = "RabinOS v0.0.1";
        int tw = font_str_width(name);
        int tx = w - tw - 16;
        int ty = h - FONT_H - 12;
        font_draw_str(tx, ty, name,
                      COLOR_RGB(0x40, 0x60, 0x80),
                      COLOR_TRANSPARENT);
    }

    wm_invalidate_all();
}

/* -------------------------------------------------------------------------
 * desktop_draw_taskbar
 * ------------------------------------------------------------------------- */

void desktop_draw_taskbar(void)
{
    int w   = (int)fb_width();
    int ty  = (int)fb_height() - TASKBAR_H;

    /* Taskbar background */
    gfx_fill_rect(0, ty, w, TASKBAR_H, COLOR_TASKBAR);
    /* Top border line */
    gfx_draw_line(0, ty, w, ty, COLOR_RGB(0x33, 0x33, 0x33));

    /* "Start"-style label */
    gfx_fill_round_rect(4, ty + 4, 56, TASKBAR_H - 8, 4,
                        COLOR_RGB(0x22, 0x55, 0xAA));
    font_draw_str(10, ty + (TASKBAR_H - FONT_H) / 2, "START",
                  COLOR_WHITE, COLOR_TRANSPARENT);

    /* Window buttons – one per open window, sorted by creation order */
    {
        int nx = TASKBAR_BTN_X0;
        int n  = wm_get_count();
        wm_window_t *focused = wm_focused();

        /* Build a local sorted array (insertion-sort by creation_idx) */
        wm_window_t *sorted[WM_MAX_WINDOWS];
        int sc = 0, si, sj;
        for (si = 0; si < n; si++) {
            wm_window_t *win = wm_get_at(si);
            if (win) sorted[sc++] = win;
        }
        /* insertion sort ascending creation_idx */
        for (si = 1; si < sc; si++) {
            wm_window_t *key = sorted[si];
            sj = si - 1;
            while (sj >= 0 && sorted[sj]->creation_idx > key->creation_idx) {
                sorted[sj + 1] = sorted[sj];
                sj--;
            }
            sorted[sj + 1] = key;
        }

        for (si = 0; si < sc; si++) {
            wm_window_t *win = sorted[si];
            if (nx + TASKBAR_BTN_W > w - 4) break;

            int is_focused = (win == focused);
            color_t btn_col = is_focused
                ? COLOR_RGB(0x33, 0x66, 0xCC)
                : COLOR_RGB(0x20, 0x20, 0x30);

            gfx_fill_round_rect(nx, ty + 3, TASKBAR_BTN_W, TASKBAR_H - 6, 3, btn_col);
            gfx_draw_rect(nx, ty + 3, TASKBAR_BTN_W, TASKBAR_H - 6,
                          is_focused ? COLOR_RGB(0x55,0x88,0xFF)
                                     : COLOR_RGB(0x44,0x44,0x66));

            /* Truncated title (max 14 chars) */
            char trunc[17];
            int  tl = 0;
            while (win->title[tl] && tl < 14) { trunc[tl] = win->title[tl]; tl++; }
            if (win->title[tl]) { trunc[tl++] = '.'; trunc[tl++] = '.'; }
            trunc[tl] = '\0';

            font_draw_str(nx + 6, ty + (TASKBAR_H - FONT_H) / 2,
                          trunc, COLOR_WHITE, COLOR_TRANSPARENT);

            nx += TASKBAR_BTN_W + 4;
        }
    }
}

/* -------------------------------------------------------------------------
 * desktop_draw_icons
 * ------------------------------------------------------------------------- */

static void desktop_draw_icons(void)
{
    int i;
    for (i = 0; i < s_icon_count; i++) {
        icon_slot_t *ic = &s_icons[i];
        if (!ic->used) continue;

        /* Icon placeholder square */
        gfx_fill_round_rect(ic->x, ic->y, 48, 48, 6,
                            COLOR_RGB(0x20, 0x40, 0x80));
        gfx_draw_rect(ic->x, ic->y, 48, 48, COLOR_RGB(0x40, 0x70, 0xCC));

        /* Label */
        if (ic->label) {
            int lw = font_str_width(ic->label);
            int lx = ic->x + (48 - lw) / 2;
            font_draw_str(lx, ic->y + 52, ic->label,
                          COLOR_WHITE, COLOR_TRANSPARENT);
        }
    }
}

/* -------------------------------------------------------------------------
 * desktop_add_icon
 * ------------------------------------------------------------------------- */

void desktop_add_icon(int x, int y, const char *label,
                      void (*on_click)(void))
{
    int i;
    if (s_icon_count >= MAX_ICONS) return;
    icon_slot_t *ic = &s_icons[s_icon_count];
    ic->x        = x;
    ic->y        = y;
    ic->on_click = on_click;
    ic->path[0]  = '\0';
    /* Copy label into owned buffer */
    for (i = 0; i < 31 && label && label[i]; i++) ic->label_buf[i] = label[i];
    ic->label_buf[i] = '\0';
    ic->label    = ic->label_buf;
    ic->used     = 1;
    s_icon_count++;
}

void desktop_add_icon_path(const char *label, const char *path)
{
    int i;
    if (s_icon_count >= MAX_ICONS) return;

    /* Auto-position: two columns of icons on the left side */
    int col  = (s_icon_count / 8) & 1;   /* column 0 or 1 */
    int row  = s_icon_count % 8;
    int x    = 16 + col * 72;
    int y    = 16 + row * 72;

    icon_slot_t *ic = &s_icons[s_icon_count];
    ic->x        = x;
    ic->y        = y;
    ic->on_click = (void *)0;
    for (i = 0; i < 255 && path && path[i]; i++) ic->path[i] = path[i];
    ic->path[i] = '\0';
    for (i = 0; i < 31 && label && label[i]; i++) ic->label_buf[i] = label[i];
    ic->label_buf[i] = '\0';
    ic->label  = ic->label_buf;
    ic->used   = 1;
    s_icon_count++;
}

/* -------------------------------------------------------------------------
 * desktop_save_config / desktop_load_config  –  /etc/desktop.conf
 *
 * Format (one line per icon):  icon <label> <path>
 * ------------------------------------------------------------------------- */

void desktop_save_config(void)
{
    int i;
    int fd = vfs_open("/etc/desktop.conf",
                      VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) return;
    for (i = 0; i < s_icon_count; i++) {
        icon_slot_t *ic = &s_icons[i];
        if (!ic->used || ic->path[0] == '\0') continue;  /* skip built-ins */
        vfs_write(fd, "icon ", 5);
        vfs_write(fd, ic->label_buf, strlen(ic->label_buf));
        vfs_write(fd, " ", 1);
        vfs_write(fd, ic->path, strlen(ic->path));
        vfs_write(fd, "\n", 1);
    }
    vfs_close(fd);
}

void desktop_load_config(void)
{
    int fd = vfs_open("/etc/desktop.conf", VFS_O_RDONLY);
    if (fd < 0) return;

    char buf[2048];
    int  total = 0, n;
    while ((n = vfs_read(fd, buf + total,
                         (int)sizeof(buf) - total - 1)) > 0)
        total += n;
    vfs_close(fd);
    buf[total] = '\0';

    /* Parse lines: "icon <label> <path>" */
    int i = 0;
    while (i < total) {
        /* Skip whitespace/blank lines */
        while (i < total && (buf[i] == '\n' || buf[i] == '\r' || buf[i] == ' '))
            i++;
        if (i >= total) break;

        /* Read keyword */
        if (buf[i] != 'i') { while (i < total && buf[i] != '\n') i++; continue; }
        i += 5; /* skip 'icon ' */
        if (i >= total) break;

        /* Read label (up to first space) */
        char label[32]; int li = 0;
        while (i < total && buf[i] != ' ' && buf[i] != '\n' && li < 31)
            label[li++] = buf[i++];
        label[li] = '\0';
        if (i < total && buf[i] == ' ') i++;

        /* Read path (to newline) */
        char path[256]; int pi = 0;
        while (i < total && buf[i] != '\n' && buf[i] != '\r' && pi < 255)
            path[pi++] = buf[i++];
        path[pi] = '\0';

        if (li > 0 && pi > 0)
            desktop_add_icon_path(label, path);
    }
}

/* -------------------------------------------------------------------------
 * check_taskbar_click – raised the clicked window
 * ------------------------------------------------------------------------- */

static void check_taskbar_click(int mx, int my)
{
    int i;
    int ty = (int)fb_height() - TASKBAR_H;
    if (my < ty || my >= ty + TASKBAR_H) return;

    int nx = TASKBAR_BTN_X0;
    int n  = wm_get_count();
    int w  = (int)fb_width();

    for (i = 0; i < n; i++) {
        wm_window_t *win = wm_get_at(i);
        if (!win) { nx += TASKBAR_BTN_W + 4; continue; }
        if (nx + TASKBAR_BTN_W > w - 4) break;

        if (mx >= nx && mx < nx + TASKBAR_BTN_W) {
            wm_raise(win);
            return;
        }
        nx += TASKBAR_BTN_W + 4;
    }
}

/* -------------------------------------------------------------------------
 * Icon click detection helper (double-click)
 * ------------------------------------------------------------------------- */

static void check_icon_clicks(int mx, int my, unsigned int now)
{
    int i;
    for (i = 0; i < s_icon_count; i++) {
        icon_slot_t *ic = &s_icons[i];
        if (!ic->used) continue;
        if (mx >= ic->x && mx < ic->x + 48 &&
            my >= ic->y && my < ic->y + 48) {
            if (s_last_click_icon == i &&
                (now - s_last_click_tick) < DBLCLICK_TICKS) {
                /* Double-click – fire the action */
                if (ic->on_click) {
                    ic->on_click();
                } else if (ic->path[0]) {
                    /* Copy path then spawn task */
                    int k;
                    for (k = 0; k < 255; k++) s_icon_launch_path[k] = ic->path[k];
                    s_icon_launch_path[255] = '\0';
                    process_t *p = process_create("icon-rox", icon_rox_task, 5);
                    if (p) sched_add(p);
                }
                s_last_click_icon = -1;
            } else {
                s_last_click_icon = i;
                s_last_click_tick = now;
            }
            return;
        }
    }
    s_last_click_icon = -1;
}

/* -------------------------------------------------------------------------
 * desktop_init
 * ------------------------------------------------------------------------- */

void desktop_init(void)
{
    fb_fill(COLOR_DESKTOP_BG);
    desktop_draw_wallpaper();
    desktop_draw_icons();
    desktop_draw_taskbar();
    fb_flush();
    log_info("[desktop] desktop initialised (%ux%u)",
             fb_width(), fb_height());

    /* Restore icons saved by previous session */
    desktop_load_config();
}

/* -------------------------------------------------------------------------
 * desktop_run  (never returns)
 * ------------------------------------------------------------------------- */

void desktop_run(void)
{
    mouse_state_t prev_ms  = mouse_get();
    unsigned int  last_frame_tick = pit_get_ticks();
    int           bg_dirty = 1;   /* draw wallpaper on first frame */

    for (;;) {
        unsigned int now = pit_get_ticks();

        /* --- Keyboard events (always polled, not rate-limited) --- */
        if (keyboard_available()) {
            int c = keyboard_read_char();
            if (c > 0)
                wm_dispatch_key((char)c);
        }

        /* --- Mouse events (always polled) --- */
        {
            mouse_state_t ms = mouse_get();
            int lbtn      = ms.buttons & 0x01;
            int prev_lbtn = prev_ms.buttons & 0x01;
            if (ms.x != prev_ms.x || ms.y != prev_ms.y ||
                ms.buttons != prev_ms.buttons) {
                wm_dispatch_mouse(ms.x, ms.y, ms.buttons);
                if (lbtn && !prev_lbtn) {
                    /* Button press can move a window → need bg redraw */
                    bg_dirty = 1;
                }
                if (lbtn && (ms.x != prev_ms.x || ms.y != prev_ms.y)) {
                    /* Mouse moving with button held = possible drag → repaint bg */
                    bg_dirty = 1;
                }
                if (!lbtn && prev_lbtn) {
                    int tb_y = (int)fb_height() - TASKBAR_H;
                    if (ms.y >= tb_y) {
                        check_taskbar_click(ms.x, ms.y);
                    } else {
                        check_icon_clicks(ms.x, ms.y, now);
                    }
                }
            }
            prev_ms = ms;
        }

        /* --- Frame render – only at FRAME_TICKS cadence --- */
        if ((now - last_frame_tick) >= FRAME_TICKS) {
            last_frame_tick = now;

            /*
             * Only redraw wallpaper when bg_dirty.
             * Windows are always repainted on top; their content is the
             * dominant cost so skipping wallpaper saves ~20% back-buffer
             * writes on a static desktop.
             */
            if (bg_dirty) {
                desktop_draw_wallpaper();
                desktop_draw_icons();
                bg_dirty = 0;
            }

            wm_invalidate_all();   /* marks all windows dirty → on_paint */
            wm_paint_all();
            desktop_draw_taskbar();
            mouse_draw_cursor();

            /* Flush back-buffer → MMIO (uses rep movsd fast path) */
            fb_flush();
        }
    }
}
