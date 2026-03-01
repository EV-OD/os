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
#include "gui/desktop.h"
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

    desktop_init();

    /* Desktop icons – double-click to open */
    desktop_add_icon(16,  16, "Terminal", desktop_spawn_terminal);

    /* Restore path icons saved from last session (e.g. /etc/desktop.con).
     * User-configured icons such as "rxt" come from here; no hard-coded
     * RXT shortcut is added so the config file is the single source of truth. */
    desktop_load_config();

    log_info("[gui] gui_launch complete");
    return 0;
}
