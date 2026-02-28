#ifndef GUI_INIT_H
#define GUI_INIT_H

/* =========================================================================
 * gui/gui_init.h – GUI subsystem orchestrator
 *
 * Single entry-point that initialises every GUI component in the correct
 * dependency order and launches the desktop.
 *
 * Call sequence (invoked from kmain after kernel_init):
 *
 *   gui_init(mb)          ← initialise framebuffer, font, gfx, mouse, WM
 *      │
 *      └─ fb_init(mb)     ← must succeed; on failure returns -1
 *      └─ mouse_init()    ← safe to fail (no mouse = keyboard-only mode)
 *      └─ wm_init()
 *
 *   desktop_run()         ← called after gui_init(); never returns
 *
 * ========================================================================= */

#include "multiboot.h"

/**
 * Initialise all GUI subsystems.
 *
 * @param mb  Pointer to the Multiboot info structure (passed from kmain).
 * @return    0 on success.
 *            -1 if the framebuffer is unavailable (GRUB returned text mode).
 *             In that case the caller should fall back to shell_run().
 *
 * On success the global framebuffer, window manager, and mouse driver are
 * ready to use.  The desktop wallpaper / taskbar are NOT drawn here –
 * call desktop_init() followed by desktop_run() for that.
 */
int gui_init(multiboot_info_t *mb);

/**
 * Return 1 if gui_init() completed successfully, 0 otherwise.
 */
int gui_ready(void);

/**
 * gui_launch – one-call helper for runtime GUI activation (e.g. 'mode gui').
 *
 * Calls gui_init(), creates a full-screen Terminal window, wires a
 * gui_term as the active terminal, and calls desktop_init().
 * After this the shell's readline() loop drives repainting via
 * gt_get_char().
 *
 * @return  0 on success, -1 if no VESA framebuffer available.
 */
int gui_launch(multiboot_info_t *mb);

#endif /* GUI_INIT_H */
