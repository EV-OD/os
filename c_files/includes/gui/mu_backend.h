#ifndef GUI_MU_BACKEND_H
#define GUI_MU_BACKEND_H

/* =========================================================================
 * gui/mu_backend.h – microui rendering backend for RandomOS
 *
 * Bridges rxi's microui immediate-mode GUI library to the OS's canvas-based
 * window manager.  The backend:
 *   - Creates a wm_window and renders microui commands to its canvas.
 *   - Polls mouse/keyboard and feeds events to the mu_Context.
 *   - Provides mu_demo_run() as a kernel process entry point.
 * ========================================================================= */

/**
 * mu_demo_run – kernel process entry.
 *
 * Opens a window, runs a microui demo with buttons/labels/panels,
 * and exits cleanly when the window is closed.
 * Intended to be launched via process_create("mu_demo", mu_demo_run, 0).
 */
void mu_demo_run(void);

#endif /* GUI_MU_BACKEND_H */
