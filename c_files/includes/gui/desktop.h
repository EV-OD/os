#ifndef GUI_DESKTOP_H
#define GUI_DESKTOP_H

/* =========================================================================
 * gui/desktop.h – Desktop environment
 *
 * The desktop owns the main event loop and draws:
 *   - Wallpaper (gradient + OS name / version)
 *   - Taskbar   (bottom strip with clock, app buttons, system menu)
 *   - Icons     (clickable shortcuts that spawn windows)
 *
 * Entry point: desktop_run() — never returns.
 * ========================================================================= */

/* Height of the bottom taskbar in pixels. */
#define TASKBAR_H      32

/* Pixels reserved at the start of the taskbar for the START button. */
#define TASKBAR_BTN_X0 68

/* Width of each window button in the taskbar. */
#define TASKBAR_BTN_W  130

/* -------------------------------------------------------------------------
 * Desktop icons
 * ------------------------------------------------------------------------- */

/** A clickable desktop icon. */
typedef struct {
    int  x, y;           /**< Top-left of the icon image (48×48 px). */
    const char *label;   /**< Label drawn below the icon. */
    void (*on_click)(void); /**< Opens a window or runs an action. */
} desktop_icon_t;

/* -------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

/**
 * Initialise the desktop: draw wallpaper, taskbar, icons.
 * Requires fb_init(), wm_init(), and mouse_init() to have been called.
 */
void desktop_init(void);

/**
 * Add a desktop icon.
 * @param x, y      Position of the icon graphic.
 * @param label     Short label (displayed below the icon).
 * @param on_click  Callback invoked when the user clicks the icon.
 */
void desktop_add_icon(int x, int y, const char *label,
                      void (*on_click)(void));

/**
 * Enter the desktop event loop.  This function never returns.
 *
 * The loop performs:
 *   1. Read keyboard character (non-blocking).
 *   2. Read mouse state.
 *   3. Dispatch events to WM.
 *   4. wm_paint_all().
 *   5. Redraw taskbar over the composited frame.
 *   6. mouse_draw_cursor().
 *   7. fb_flush().
 */
void desktop_run(void);

/**
 * Redraw only the taskbar strip.
 * Called by the event loop each frame (cheap: ~32 px tall).
 */
void desktop_draw_taskbar(void);

/**
 * Redraw only the wallpaper and desktop icons.
 * Automatically marks all WM windows as dirty.
 */
void desktop_draw_wallpaper(void);

/**
 * Spawn a new GUI terminal window + shell task.
 * Called from the Terminal icon double-click callback.
 */
void desktop_spawn_terminal(void);

/**
 * Launch a new rxt editor instance (non-blocking: spawns a kernel task).
 * Called from the RXT icon double-click callback.
 */
void desktop_open_rxt(void);

/**
 * Add a path-based desktop icon whose double-click loads path as a .rox program.
 * The label is copied internally; the icon position is chosen automatically.
 */
void desktop_add_icon_path(const char *label, const char *path);

/**
 * Persist the current path-based icons to /etc/desktop.con.
 * Built-in icons (Terminal, RXT) are NOT saved – they are registered at boot.
 */
void desktop_save_config(void);

/**
 * Load /etc/desktop.con and register the icons found there.
 * Called from desktop_init() after the built-in icons have been registered.
 */
void desktop_load_config(void);

/**
 * Remove a path-based desktop icon by its label (case-sensitive).
 * Also persists the updated list to /etc/desktop.con.
 * Built-in icons (no path) are never removed.
 * @return 0 if found and removed, -1 if not found.
 */
int desktop_remove_icon(const char *label);

#endif /* GUI_DESKTOP_H */
