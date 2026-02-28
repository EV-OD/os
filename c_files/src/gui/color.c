/* =========================================================================
 * gui/color.c – Color utilities
 *
 * Provides the VGA 16-colour index → color_t mapping.
 * The header (color.h) defines all the macros; this file only implements
 * color_from_vga().
 * ========================================================================= */

#include "gui/color.h"

/* ---------------------------------------------------------------------------
 * Classic CGA / VGA 16-colour palette (indices 0-15)
 * -------------------------------------------------------------------------- */
static const color_t s_vga16[16] = {
    /* 0  */ TCOLOR_BLACK,
    /* 1  */ TCOLOR_BLUE,
    /* 2  */ TCOLOR_GREEN,
    /* 3  */ TCOLOR_CYAN,
    /* 4  */ TCOLOR_RED,
    /* 5  */ TCOLOR_MAGENTA,
    /* 6  */ TCOLOR_YELLOW,   /* brown in true CGA; yellow here */
    /* 7  */ TCOLOR_WHITE,
    /* 8  */ TCOLOR_BR_BLACK,
    /* 9  */ TCOLOR_BR_BLUE,
    /* 10 */ TCOLOR_BR_GREEN,
    /* 11 */ TCOLOR_BR_CYAN,
    /* 12 */ TCOLOR_BR_RED,
    /* 13 */ TCOLOR_BR_MAGENTA,
    /* 14 */ TCOLOR_BR_YELLOW,
    /* 15 */ TCOLOR_BR_WHITE,
};

color_t color_from_vga(unsigned char idx)
{
    return s_vga16[idx & 0x0F];
}
