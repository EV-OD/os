#ifndef GUI_MOUSE_H
#define GUI_MOUSE_H

/* =========================================================================
 * gui/mouse.h – PS/2 mouse driver
 *
 * Implements the PS/2 auxiliary-port mouse protocol.  Data arrives via
 * IRQ12 in 3-byte packets; the driver accumulates them in a static buffer
 * and converts delta movements to absolute screen coordinates.
 *
 * Usage:
 *   1. Call mouse_init() once (after pic_remap and isr_install).
 *   2. Call mouse_get() from the desktop/WM event loop to read the current
 *      pointer state.
 *   3. Call mouse_draw_cursor() each frame AFTER compositing all windows.
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * Mouse state
 * ------------------------------------------------------------------------- */

/** Snapshot of the current mouse state. */
typedef struct {
    int     x;        /**< Absolute X position (0 = left edge). */
    int     y;        /**< Absolute Y position (0 = top edge).  */
    unsigned char buttons; /**< Bitmask: bit 0=left, bit 1=right, bit 2=middle. */
} mouse_state_t;

/* -------------------------------------------------------------------------
 * Driver API
 * ------------------------------------------------------------------------- */

/**
 * Initialise the PS/2 mouse.
 *
 * Sends the initialisation sequence to the 8042 controller, enables
 * data reporting, and registers the IRQ12 handler so the driver can
 * accumulate 3-byte packets autonomously.
 *
 * Prerequisites: isr_install() must have been called; pic_remap() must have
 * unmasked IRQ12 (PIC2 cascade).
 */
void mouse_init(void);

/**
 * Return a snapshot of the current mouse state (position + button flags).
 * Thread-safe via a simple busy-wait-free atomic copy.
 */
mouse_state_t mouse_get(void);

/**
 * Draw the hardware cursor sprite at the current mouse position.
 *
 * Must be called once per frame, AFTER all windows have been composited into
 * the framebuffer back-buffer and BEFORE fb_flush().  The function saves the
 * pixels behind the cursor so they can be restored on the next frame (no
 * compositor needed).
 */
void mouse_draw_cursor(void);

/**
 * Return 1 if mouse_init() has been called successfully, 0 otherwise.
 */
int mouse_available(void);

#endif /* GUI_MOUSE_H */
