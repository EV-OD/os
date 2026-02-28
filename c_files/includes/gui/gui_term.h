#ifndef GUI_TERM_H
#define GUI_TERM_H

/* =========================================================================
 * gui/gui_term.h – Framebuffer terminal (implements terminal_t)
 *
 * Provides a scrolling text terminal rendered onto a WM window's canvas
 * using the embedded 8×16 bitmap font.
 *
 * The GUI terminal:
 *   - Maintains an internal character grid (cols × rows).
 *   - Handles \n, \r, \b, tab (\t aligned to 8 cols).
 *   - Scrolls the grid up by one line when the cursor reaches the last row.
 *   - Buffers keyboard input in a 256-byte ring (fed by IRQ1 via keyboard.h).
 *   - Exposes a terminal_t vtable so the shell works unchanged.
 *
 * Usage:
 *   wm_window_t *win = wm_create(50, 50, 640, 400, "Terminal");
 *   terminal_t  *t   = gui_term_create(win);
 *   term_set_active(t);
 *   shell_run();          // runs inside the GUI terminal window
 * ========================================================================= */

#include "terminal.h"          /* terminal_t vtable definition */
#include "gui/wm.h"

/* -------------------------------------------------------------------------
 * Constructor
 * ------------------------------------------------------------------------- */

/**
 * Create a GUI terminal backed by @p win.
 *
 * The function:
 *   1. Derives the character grid dimensions from the window's client area
 *      (client_w / FONT_W columns, client_h / FONT_H rows).
 *   2. Allocates a `gui_term_state_t` with kmalloc.
 *   3. Sets win->on_paint / win->on_key to the terminal's handlers.
 *   4. Returns a populated terminal_t that can be passed to term_set_active().
 *
 * @param win  The window that will host the terminal (must be non-NULL).
 *             The window should have been created with wm_create().
 * @return     A heap-allocated terminal_t or NULL on allocation failure.
 */
terminal_t *gui_term_create(wm_window_t *win);

/**
 * Destroy a GUI terminal created with gui_term_create().
 * Frees the internal state; the wm_window_t itself must be destroyed
 * separately with wm_destroy().
 *
 * @param t  Pointer previously returned by gui_term_create().
 */
void gui_term_destroy(terminal_t *t);

/* -------------------------------------------------------------------------
 * Optional: direct access to the internal ring buffer
 * ------------------------------------------------------------------------- */

/**
 * Push a character into the terminal's keyboard ring buffer.
 * Normally called by the keyboard IRQ handler; exposed here so the
 * desktop can forward key events to the active terminal.
 *
 * @param t  Terminal returned by gui_term_create().
 * @param c  Character to enqueue.
 */
void gui_term_push_char(terminal_t *t, char c);

#endif /* GUI_TERM_H */
