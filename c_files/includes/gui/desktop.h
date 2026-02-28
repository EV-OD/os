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
#define TASKBAR_H  32

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

#endif /* GUI_DESKTOP_H */
