/* =========================================================================
 * terminal.c – Terminal abstraction implementation
 *
 * Provides:
 *   - The global active-terminal singleton (term_active / term_set_active)
 *   - term_vga_init(): wires the classic VGA text-mode I/O functions
 *     (putchar, puts, getchar, readline, fb_clear, printf) into a
 *     terminal_t vtable so the shell can treat VGA and GUI terminals
 *     identically.
 *
 * See c_files/includes/terminal.h for the API.
 * ========================================================================= */

#include "terminal.h"
#include "stdio.h"   /* putchar, puts, getchar, readline, fb_clear, printf */

/* -------------------------------------------------------------------------
 * Active-terminal singleton
 * ------------------------------------------------------------------------- */
static terminal_t *s_active = (terminal_t *)0;

terminal_t *term_active(void)
{
    return s_active;
}

void term_set_active(terminal_t *t)
{
    s_active = t;
}

/* -------------------------------------------------------------------------
 * VGA text-mode terminal
 *
 * The wrappers below cast away const for the put_string variants because
 * the classic puts() / puts_color() take non-const char* but are safe to
 * call with const data.
 * ------------------------------------------------------------------------- */

static void vga_put_char(char c)
{
    /* Use putchar_raw to avoid re-entering putchar() when term_active()
     * points to the VGA terminal (would cause infinite recursion). */
    putchar_raw(c);
}

static void vga_put_char_color(char c, unsigned char fg)
{
    putchar_color_raw(c, fg);
}

static void vga_put_string(const char *s)
{
    puts((char *)s);
}

static void vga_put_string_color(const char *s, unsigned char fg)
{
    puts_color(s, fg);
}

/* The VGA terminal's get_char reads directly from the keyboard ring buffer
 * (via read_char_blocking) so that stdio.c::getchar() can safely delegate
 * to term_active()->get_char() without triggering infinite recursion. */
static int vga_get_char(void)
{
    int ch = read_char_blocking();
    putchar((char)ch);   /* echo */
    return ch;
}

static terminal_t s_vga_term = {
    .put_char        = vga_put_char,
    .put_char_color  = vga_put_char_color,
    .put_string      = vga_put_string,
    .put_string_color= vga_put_string_color,
    .get_char        = vga_get_char,
    .read_line       = readline,
    .clear           = fb_clear,
    .tprintf         = printf,
};

terminal_t *term_vga_init(void)
{
    return &s_vga_term;
}
