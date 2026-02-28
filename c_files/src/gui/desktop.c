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
#include "keyboard.h"
#include "log.h"
#include "string.h"
#include "pit.h"   /* pit_get_ticks() – frame rate limiter */

/* Target frame period: 3 ticks × 10 ms = ~33 fps */
#define FRAME_TICKS  3u

/* -------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------- */

#define MAX_ICONS  16

typedef struct {
    int  x, y;
    const char *label;
    void (*on_click)(void);
    int  used;
} icon_slot_t;

static icon_slot_t s_icons[MAX_ICONS];
static int         s_icon_count = 0;

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
    if (s_icon_count >= MAX_ICONS) return;
    s_icons[s_icon_count].x        = x;
    s_icons[s_icon_count].y        = y;
    s_icons[s_icon_count].label    = label;
    s_icons[s_icon_count].on_click = on_click;
    s_icons[s_icon_count].used     = 1;
    s_icon_count++;
}

/* -------------------------------------------------------------------------
 * Icon click detection helper
 * ------------------------------------------------------------------------- */

static void check_icon_clicks(int mx, int my)
{
    int i;
    for (i = 0; i < s_icon_count; i++) {
        icon_slot_t *ic = &s_icons[i];
        if (!ic->used) continue;
        if (mx >= ic->x && mx < ic->x + 48 &&
            my >= ic->y && my < ic->y + 48) {
            if (ic->on_click) ic->on_click();
        }
    }
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
}

/* -------------------------------------------------------------------------
 * desktop_run  (never returns)
 * ------------------------------------------------------------------------- */

void desktop_run(void)
{
    mouse_state_t prev_ms = mouse_get();
    unsigned int  last_frame_tick = pit_get_ticks();

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
                if (!lbtn && prev_lbtn) {
                    int tb_y = (int)fb_height() - TASKBAR_H;
                    if (ms.y < tb_y)
                        check_icon_clicks(ms.x, ms.y);
                }
            }
            prev_ms = ms;
        }

        /* --- Frame render – only at FRAME_TICKS cadence --- */
        if ((now - last_frame_tick) >= FRAME_TICKS) {
            last_frame_tick = now;

            desktop_draw_wallpaper();
            desktop_draw_icons();
            wm_invalidate_all();   /* marks all windows dirty → on_paint */
            wm_paint_all();
            desktop_draw_taskbar();
            mouse_draw_cursor();
            fb_flush();
        }
    }
}
