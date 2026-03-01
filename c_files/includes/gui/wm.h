#ifndef GUI_WM_H
#define GUI_WM_H

/* =========================================================================
 * gui/wm.h – Software window manager
 *
 * A lightweight, software-rendered window manager that maintains a list of
 * windows in Z-order and dispatches keyboard/mouse events to the focused
 * window.
 *
 * Design constraints:
 *  - Fixed-size window stack (WM_MAX_WINDOWS).
 *  - No memory allocation in hot paths (paint loop).
 *  - Each window owns its own pixel canvas (kmalloc'd 32-bpp buffer).
 *  - Compositing is performed by blitting each canvas into the framebuffer
 *    back-buffer in back→front Z order, then drawing the title bar on top.
 *
 * Title bar layout (TITLE_BAR_H pixels tall):
 *   ┌─────────────────────────────── [X]─┐
 *   │  title text                         │
 *   ├─────────────────────────────────────┤
 *   │  canvas  (w × (h - TITLE_BAR_H))   │
 *   └─────────────────────────────────────┘
 * ========================================================================= */

/* Maximum number of concurrently open windows. */
#define WM_MAX_WINDOWS   16

/* Height of the title bar in pixels. */
#define TITLE_BAR_H      22

/* -------------------------------------------------------------------------
 * Window descriptor
 * ------------------------------------------------------------------------- */

typedef struct wm_window wm_window_t;

struct wm_window {
    /* --- Geometry ------------------------------------------------------- */
    int  x, y;              /**< Screen position of the window's top-left.        */
    int  w, h;              /**< Total window size including title bar.            */

    /* --- Identity ------------------------------------------------------- */
    char title[64];         /**< Title bar text (NUL-terminated).                 */

    /* --- Canvas --------------------------------------------------------- */
    /**
     * Pixel buffer for the window's client area:
     *   width  = w
     *   height = h - TITLE_BAR_H
     *   format = 32-bpp 0x00RRGGBB row-major
     *   stride = w (tightly packed)
     * Allocated by wm_create(); freed by wm_destroy().
     */
    unsigned int *canvas;

    /* --- State ---------------------------------------------------------- */
    int visible;            /**< Non-zero if the window should be composited.     */
    int dirty;              /**< Non-zero if canvas was changed and needs repaint.*/

    /* --- Callbacks ------------------------------------------------------ */
    /** Called by the WM when the window should redraw its canvas. */
    void (*on_paint )( wm_window_t *win );

    /** Called by the WM to deliver a keyboard character. */
    void (*on_key   )( wm_window_t *win, char c );

    /** Called by the WM to deliver a mouse event (position relative to
     *  the window's client area, button bitmask). */
    void (*on_mouse )( wm_window_t *win, int rx, int ry, unsigned char btns );

    /* --- Internal (do not touch) --------------------------------------- */
    int _stack_idx;         /**< Position in wm_stack[]; managed by the WM.      */
    int no_drag;            /**< Non-zero: disables title-bar drag for this win.  */
    int owner_pid;          /**< PID of the process that owns this window (0=none). */

    /* --- Drawing state ------------------------------------------------- */
    unsigned int pen_color; /**< Current stroke colour (used by line/rect/circle). */
    unsigned int bg_color;  /**< Canvas background fill colour.                   */
};

/* -------------------------------------------------------------------------
 * Window manager API
 * ------------------------------------------------------------------------- */

/**
 * Initialise the window manager.  Must be called once before any other
 * wm_* function (after fb_init()).
 */
void wm_init(void);

/**
 * Create a new window and push it to the top of the Z-stack.
 *
 * @param x, y   Initial screen position of the window's top-left corner.
 * @param w, h   Window size INCLUDING the title bar.
 * @param title  Title bar text (copied; max 63 chars).
 * @return       Pointer to the new window descriptor, or NULL if the window
 *               stack is full or kmalloc fails.
 */
wm_window_t *wm_create(int x, int y, int w, int h, const char *title);

/**
 * Allocate and zero-initialise the canvas buffer for a window.
 *
 * Must be called after wm_create() for windows that need an offscreen
 * pixel buffer (i.e. windows opened via SYS_GUI_OPEN).
 * System-terminal windows skip this to avoid wasting ~3 MB of heap.
 *
 * The canvas is sized  win->w × (win->h - TITLE_BAR_H)  pixels (32-bpp).
 * Returns non-zero on success, 0 if kmalloc fails.
 */
int wm_alloc_canvas(wm_window_t *win);

/**
 * Destroy a window: free its canvas, remove it from the stack.
 * The pointer @p win must not be used after this call.
 */
void wm_destroy(wm_window_t *win);

/**
 * Bring @p win to the front of the Z-stack (make it the focused window).
 */
void wm_raise(wm_window_t *win);

/**
 * Return a pointer to the currently focused (top-most) window,
 * or NULL if no windows exist.
 */
wm_window_t *wm_focused(void);

/* -------------------------------------------------------------------------
 * Painting
 * ------------------------------------------------------------------------- */

/**
 * Composite all visible windows onto the framebuffer back-buffer in Z-order
 * (back→front).  Calls each window's on_paint() callback if dirty.
 * Does NOT flush the back-buffer to the screen.
 */
void wm_paint_all(void);

/**
 * Composite a single window into the framebuffer back-buffer.
 * Draws the canvas, title bar, border, and close button.
 */
void wm_paint(wm_window_t *win);

/* -------------------------------------------------------------------------
 * Event dispatch
 * ------------------------------------------------------------------------- */

/**
 * Deliver a keyboard character to the focused window.
 * @param c  The character (ASCII or special key code).
 */
void wm_dispatch_key(char c);

/**
 * Deliver a mouse event to the appropriate window (hit-test, then dispatch).
 * Handles window raising on click, dragging by title bar, and close button.
 *
 * @param mx, my  Current absolute mouse position.
 * @param btns    Button bitmask (bit 0=left, 1=right, 2=middle).
 */
void wm_dispatch_mouse(int mx, int my, unsigned char btns);

/**
 * Mark all windows as dirty (forces a full repaint on the next
 * wm_paint_all() call).  Used after the desktop background is redrawn.
 */
void wm_invalidate_all(void);

/**
 * Destroy all windows owned by a specific process.
 * Called by the scheduler when a user process dies to clean up orphaned windows.
 *
 * @param pid  PID of the deceased process.
 */
void wm_destroy_by_pid(int pid);

#endif /* GUI_WM_H */
