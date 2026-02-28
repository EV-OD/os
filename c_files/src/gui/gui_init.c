/* =========================================================================
 * gui/gui_init.c – GUI subsystem orchestrator
 *
 * Initialises every GUI component in the correct dependency order:
 *   fb_init  → mouse_init → wm_init
 *
 * After gui_init() succeeds, call desktop_init() then desktop_run().
 * ========================================================================= */

#include "gui/gui_init.h"
#include "gui/fb.h"
#include "gui/mouse.h"
#include "gui/wm.h"
#include "gui/gui_term.h"
#include "gui/desktop.h"
#include "terminal.h"
#include "multiboot.h"
#include "log.h"

static int s_gui_ready = 0;

int gui_init(multiboot_info_t *mb)
{
    log_info("[gui] initialising GUI subsystem");

    /* 1. Framebuffer – must succeed; everything else depends on it. */
    if (fb_init(mb) < 0) {
        log_warning("[gui] framebuffer unavailable – falling back to nerd mode");
        return -1;
    }

    /* 2. PS/2 mouse – soft failure (continue without mouse) */
    mouse_init();

    /* 3. Window manager */
    wm_init();

    s_gui_ready = 1;
    log_info("[gui] GUI subsystem ready (%ux%u)",
             fb_width(), fb_height());
    return 0;
}

int gui_ready(void)
{
    return s_gui_ready;
}

/**
 * gui_launch – full one-call setup for 'mode gui' from the shell.
 *
 * Calls gui_init(), creates a full-screen terminal window, wires the
 * GUI terminal as the active terminal, and calls desktop_init().
 * After this the shell's readline() loop drives the compositor because
 * getchar() delegates through term_active()->get_char() = gt_get_char().
 *
 * @return  0 on success, -1 if no VESA framebuffer was provided by GRUB.
 */
int gui_launch(multiboot_info_t *mb)
{
    if (gui_init(mb) != 0) {
        return -1;
    }

    int sw = (int)fb_width();
    int sh = (int)fb_height();

    wm_window_t *win = wm_create(0, 0, sw, sh - TASKBAR_H, "Terminal");
    if (win) {
        win->no_drag = 1;   /* full-screen terminal – disable titlebar drag */
        terminal_t *t = gui_term_create(win);
        if (t) {
            term_set_active(t);
        }
    }

    desktop_init();
    log_info("[gui] gui_launch complete – GUI terminal active");
    return 0;
}
