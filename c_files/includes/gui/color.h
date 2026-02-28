#ifndef GUI_COLOR_H
#define GUI_COLOR_H

/* =========================================================================
 * gui/color.h – Color type and named constants
 *
 * color_t is a 32-bit packed pixel in 0x00RRGGBB format.
 * This matches the most common VESA layout:
 *   red   field position  = 16, mask = 8 bits
 *   green field position  =  8, mask = 8 bits
 *   blue  field position  =  0, mask = 8 bits
 *
 * For framebuffers with a different layout fb_put_pixel() remaps the
 * channels automatically using the shift/mask values from multiboot_info_t.
 * ========================================================================= */

typedef unsigned int color_t;

/* -------------------------------------------------------------------------
 * Construction / decomposition macros
 * ------------------------------------------------------------------------- */

/** Pack three 8-bit channel values into a color_t (0x00RRGGBB). */
#define COLOR_RGB(r, g, b) \
    ((color_t)(((unsigned int)(r) << 16) | \
               ((unsigned int)(g) <<  8) | \
               ((unsigned int)(b)      )))

/** Extract the red channel (0-255). */
#define COLOR_R(c)  (((c) >> 16) & 0xFFu)

/** Extract the green channel (0-255). */
#define COLOR_G(c)  (((c) >>  8) & 0xFFu)

/** Extract the blue channel (0-255). */
#define COLOR_B(c)  (((c)      ) & 0xFFu)

/**
 * Special sentinel meaning "transparent background" in font/gfx functions.
 * Pixels with this color as the background value are NOT written to the
 * framebuffer, allowing the existing content to show through.
 */
#define COLOR_TRANSPARENT  0xFF000000u

/* -------------------------------------------------------------------------
 * Named colour constants
 *
 * Several names (COLOR_BLACK, COLOR_WHITE, COLOR_RED, COLOR_GREEN,
 * COLOR_BLUE, COLOR_CYAN, COLOR_MAGENTA) are also defined in stdio.h as
 * VGA palette indices.  We #undef them here so that whichever header was
 * included first, the 32-bit RGB definitions below always win in GUI code.
 * ------------------------------------------------------------------------- */
#undef COLOR_BLACK
#undef COLOR_WHITE
#undef COLOR_RED
#undef COLOR_GREEN
#undef COLOR_BLUE
#undef COLOR_CYAN
#undef COLOR_MAGENTA

#define COLOR_BLACK         COLOR_RGB(0x00, 0x00, 0x00)
#define COLOR_WHITE         COLOR_RGB(0xFF, 0xFF, 0xFF)
#define COLOR_GRAY          COLOR_RGB(0xAA, 0xAA, 0xAA)
#define COLOR_DKGRAY        COLOR_RGB(0x44, 0x44, 0x44)
#define COLOR_LTGRAY        COLOR_RGB(0xCC, 0xCC, 0xCC)

#define COLOR_RED           COLOR_RGB(0xFF, 0x00, 0x00)
#define COLOR_GREEN         COLOR_RGB(0x00, 0xCC, 0x00)
#define COLOR_BLUE          COLOR_RGB(0x00, 0x55, 0xFF)
#define COLOR_YELLOW        COLOR_RGB(0xFF, 0xFF, 0x00)
#define COLOR_CYAN          COLOR_RGB(0x00, 0xFF, 0xFF)
#define COLOR_MAGENTA       COLOR_RGB(0xFF, 0x00, 0xFF)
#define COLOR_ORANGE        COLOR_RGB(0xFF, 0x88, 0x00)

/* UI / desktop palette */
#define COLOR_DESKTOP_BG    COLOR_RGB(0x1A, 0x2A, 0x4A)  /* dark navy blue  */
#define COLOR_TASKBAR       COLOR_RGB(0x0D, 0x0D, 0x0D)  /* near-black      */
#define COLOR_TASKBAR_TEXT  COLOR_WHITE
#define COLOR_WINDOW_TITLE  COLOR_RGB(0x1E, 0x5C, 0xAA)  /* medium blue     */
#define COLOR_WINDOW_BORDER COLOR_DKGRAY
#define COLOR_WINDOW_BG     COLOR_RGB(0xF0, 0xF0, 0xF0)  /* light grey      */
#define COLOR_CLOSE_BTN     COLOR_RGB(0xCC, 0x22, 0x22)  /* close-button red*/
#define COLOR_CURSOR_FG     COLOR_WHITE
#define COLOR_CURSOR_BORDER COLOR_BLACK

/* Terminal colours (classic 16-colour palette) */
#define TCOLOR_BLACK        COLOR_RGB(0x00, 0x00, 0x00)
#define TCOLOR_RED          COLOR_RGB(0xAA, 0x00, 0x00)
#define TCOLOR_GREEN        COLOR_RGB(0x00, 0xAA, 0x00)
#define TCOLOR_YELLOW       COLOR_RGB(0xAA, 0x55, 0x00)
#define TCOLOR_BLUE         COLOR_RGB(0x00, 0x00, 0xAA)
#define TCOLOR_MAGENTA      COLOR_RGB(0xAA, 0x00, 0xAA)
#define TCOLOR_CYAN         COLOR_RGB(0x00, 0xAA, 0xAA)
#define TCOLOR_WHITE        COLOR_RGB(0xAA, 0xAA, 0xAA)
#define TCOLOR_BR_BLACK     COLOR_RGB(0x55, 0x55, 0x55)
#define TCOLOR_BR_RED       COLOR_RGB(0xFF, 0x55, 0x55)
#define TCOLOR_BR_GREEN     COLOR_RGB(0x55, 0xFF, 0x55)
#define TCOLOR_BR_YELLOW    COLOR_RGB(0xFF, 0xFF, 0x55)
#define TCOLOR_BR_BLUE      COLOR_RGB(0x55, 0x55, 0xFF)
#define TCOLOR_BR_MAGENTA   COLOR_RGB(0xFF, 0x55, 0xFF)
#define TCOLOR_BR_CYAN      COLOR_RGB(0x55, 0xFF, 0xFF)
#define TCOLOR_BR_WHITE     COLOR_RGB(0xFF, 0xFF, 0xFF)

/** Map a classic VGA colour index (0-15) to a color_t. */
color_t color_from_vga(unsigned char idx);

#endif /* GUI_COLOR_H */
