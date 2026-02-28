#ifndef TERMINAL_H
#define TERMINAL_H

/* =========================================================================
 * terminal.h – Terminal abstraction layer
 *
 * Separates "how things reach the screen" from "what the shell logic does".
 *
 * A terminal_t is a vtable with function pointers for output, input, and
 * control.  The shell backend operates entirely through a terminal_t*,
 * never touching VGA memory directly.
 *
 * Shipping implementations:
 *   term_vga  – classic 80×25 VGA text-mode terminal (nerd mode)
 *   (future)  – GUI-mode terminal backed by a framebuffer window
 *
 * The active terminal is a global singleton so that .rox child processes
 * can also write to it without explicitly receiving a pointer.
 * ========================================================================= */

#include "stream.h"

/* -------------------------------------------------------------------------
 * Terminal vtable
 * ------------------------------------------------------------------------- */
typedef struct terminal {
    /* --- Output --------------------------------------------------------- */
    void (*put_char)(char c);
    void (*put_char_color)(char c, unsigned char fg);
    void (*put_string)(const char *s);
    void (*put_string_color)(const char *s, unsigned char fg);

    /* --- Input ---------------------------------------------------------- */
    int  (*get_char)(void);                            /* blocking */
    int  (*read_line)(char *buf, unsigned int max);    /* blocking, handles BS */

    /* --- Control -------------------------------------------------------- */
    void (*clear)(void);

    /* --- Formatted output ----------------------------------------------- */
    int  (*tprintf)(const char *fmt, ...);
} terminal_t;

/* -------------------------------------------------------------------------
 * Active terminal (global singleton)
 * ------------------------------------------------------------------------- */

/** Return a pointer to the currently active terminal. */
terminal_t *term_active(void);

/** Set the active terminal.  Shell and .rox processes use this. */
void term_set_active(terminal_t *t);

/* -------------------------------------------------------------------------
 * VGA text-mode terminal
 * ------------------------------------------------------------------------- */

/** Initialise and return the VGA text-mode terminal.
 *  Wires up putchar/puts/getchar/readline/clear/printf to the VGA
 *  framebuffer and PS/2 keyboard.
 */
terminal_t *term_vga_init(void);

#endif /* TERMINAL_H */
