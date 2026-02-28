#ifndef GUI_FB_H
#define GUI_FB_H

/* =========================================================================
 * gui/fb.h – Linear framebuffer abstraction
 *
 * Wraps the VESA/VBE linear framebuffer handed over by GRUB (Multiboot 1).
 * Provides:
 *   - fb_init()          – parse multiboot info, map MMIO, alloc back-buffer
 *   - fb_put_pixel()     – write one 32-bit pixel to the back-buffer
 *   - fb_flush()         – blit entire back-buffer → front (VESA) buffer
 *   - fb_flush_rect()    – blit a dirty rectangle
 *   - fb_clear()         – fill back-buffer with a colour
 *   - fb_width/height()  – current resolution
 *
 * All drawing functions operate on the BACK buffer; call fb_flush() (or
 * fb_flush_rect()) to make changes visible.
 * ========================================================================= */

#include "multiboot.h"
#include "gui/color.h"

/* -------------------------------------------------------------------------
 * Initialisation
 * ------------------------------------------------------------------------- */

/**
 * Initialise the framebuffer from the Multiboot information structure.
 *
 * @param mb  Pointer to the multiboot info struct passed by the bootloader.
 * @return    0 on success, -1 if no RGB framebuffer is available.
 *
 * On success:
 *   - The VESA physical framebuffer is identity-mapped via paging_map_mmio().
 *   - A 32-bpp back-buffer (width × height × 4 bytes) is allocated with
 *     kmalloc().
 * On failure the caller should fall back to VGA text mode (nerd mode).
 */
int fb_init(multiboot_info_t *mb);

/**
 * Return 1 if the framebuffer has been successfully initialised, 0 otherwise.
 */
int fb_available(void);

/* -------------------------------------------------------------------------
 * Dimensions
 * ------------------------------------------------------------------------- */

/** Framebuffer width in pixels (0 if not initialised). */
unsigned int fb_width(void);

/** Framebuffer height in pixels (0 if not initialised). */
unsigned int fb_height(void);

/* -------------------------------------------------------------------------
 * Pixel operations (operate on the BACK buffer)
 * ------------------------------------------------------------------------- */

/**
 * Write a single pixel to the back-buffer.
 * Out-of-bounds coordinates are silently ignored.
 *
 * @param x     Pixel X coordinate (0 = left edge).
 * @param y     Pixel Y coordinate (0 = top edge).
 * @param color 32-bit packed colour (0x00RRGGBB).
 */
void fb_put_pixel(int x, int y, color_t color);

/**
 * Read a pixel from the back-buffer.
 * Returns 0 for out-of-bounds coordinates.
 */
color_t fb_get_pixel(int x, int y);

/**
 * Fill the entire back-buffer with @p color.
 */
void fb_fill(color_t color);

/**
 * Copy a rectangular region of pixels from @p src into the back-buffer.
 *
 * @param dst_x, dst_y  Top-left destination in the back-buffer.
 * @param w, h          Size of the rectangle.
 * @param src           Source pixel array (w × h, row-major, 32-bpp).
 * @param src_stride    Number of 32-bit words per source row
 *                      (use @p w for tightly-packed data).
 */
void fb_blit(int dst_x, int dst_y, int w, int h,
             const unsigned int *src, int src_stride);

/* -------------------------------------------------------------------------
 * Flushing (copy back-buffer → VESA front buffer)
 * ------------------------------------------------------------------------- */

/**
 * Copy the entire back-buffer to the VESA linear framebuffer.
 * Call once per frame after all drawing is done.
 */
void fb_flush(void);

/**
 * Copy a rectangular region of the back-buffer to the front buffer.
 * Use this to avoid flushing the whole screen when only a small area changed.
 *
 * @param x, y  Top-left corner of the dirty rectangle.
 * @param w, h  Size of the dirty rectangle.
 */
void fb_flush_rect(int x, int y, int w, int h);

#endif /* GUI_FB_H */
