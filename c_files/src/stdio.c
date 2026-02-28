#include "stdio.h"
#include "keyboard.h"
#include "string.h"
#ifdef GUI_MODE
#include "terminal.h"   /* term_active() – routes input through GUI terminal */
#endif

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)

/*
 * The VGA text-mode framebuffer is at physical 0x000B8000.
 * In our higher-half kernel, physical 0x00000000 is mapped to virtual
 * 0xC0000000, so the framebuffer is at virtual 0xC00B8000.
 */
volatile unsigned char *framebuffer = (unsigned char *)0xC00B8000;
static unsigned short cursor_pos = 0;

/* Total bytes in the VGA framebuffer (80 cols × 25 rows × 2 bytes/cell) */
#define FB_SIZE  (FB_COLUMNS * FB_ROWS * 2)

static int is_space(char c)
{
    return c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

/* -------------------------------------------------------------------------
 * fb_scroll_up – scroll the entire screen up by one line.
 * Copies rows 1..24 to rows 0..23, then blanks row 24.
 * ------------------------------------------------------------------------- */
static void fb_scroll_up(void)
{
    /* Each row is FB_COLUMNS * 2 bytes */
    unsigned int row_bytes = FB_COLUMNS * 2;

    /* Move rows 1..24 up to 0..23 */
    memcpy((void *)framebuffer,
           (const void *)(framebuffer + row_bytes),
           row_bytes * (FB_ROWS - 1));

    /* Clear the last row */
    unsigned int last_row_start = row_bytes * (FB_ROWS - 1);
    for (unsigned int i = 0; i < FB_COLUMNS; i++) {
        framebuffer[last_row_start + i * 2]     = FB_EMPTY_CELL;
        framebuffer[last_row_start + i * 2 + 1] = FB_DEFAULT_COLOR;
    }
}

/* -------------------------------------------------------------------------
 * fb_check_scroll – if cursor_pos is past the last row, scroll up.
 * ------------------------------------------------------------------------- */
static void fb_check_scroll(void)
{
    while (cursor_pos >= FB_SIZE) {
        fb_scroll_up();
        cursor_pos -= FB_COLUMNS * 2;
    }
    fb_move_cursor(cursor_pos / 2);
}

void fb_write_cell(unsigned int i, char c, unsigned char fg, unsigned char bg)
{
    framebuffer[i] = (unsigned char)c;
    framebuffer[i + 1] = ((fg & 0x0F) << 4) | (bg & 0x0F);
}

void fb_move_cursor(unsigned short pos)
{
    outb(FB_COMMAND_PORT, FB_HIGH_BYTE_COMMAND);
    outb(FB_DATA_PORT, (unsigned char)((pos >> 8) & 0x00FF));
    outb(FB_COMMAND_PORT, FB_LOW_BYTE_COMMAND);
    outb(FB_DATA_PORT, (unsigned char)(pos & 0x00FF));
    cursor_pos = pos * 2;
}

void fb_clear(void)
{
    for (unsigned int i = 0; i < FB_ROWS * FB_COLUMNS; i++) {
        fb_write_cell(i * 2, FB_EMPTY_CELL, COLOR_BLACK, COLOR_WHITE);
    }
}

void cursor_move_home(void)
{
    cursor_pos = 0;
    fb_move_cursor(0);
}

void cursor_move_newline(void)
{
    cursor_pos += (FB_COLUMNS - (cursor_pos / 2) % FB_COLUMNS) * 2;
    fb_move_cursor(cursor_pos / 2);
}

void cursor_move_back(void)
{
    if (cursor_pos >= 2) {
        cursor_pos -= 2;
        fb_move_cursor(cursor_pos / 2);
    }
}

void cursor_move_forward(void)
{
    cursor_pos += 2;
    fb_move_cursor(cursor_pos / 2);
}

int putchar_at(char c, unsigned short pos)
{
    unsigned int offset = (unsigned int)pos * 2;
    fb_write_cell(offset, c, COLOR_BLACK, COLOR_WHITE);
    cursor_pos = offset + 2;
    fb_move_cursor(cursor_pos / 2);
    return 0;
}

int puts_at(char *buf)
{
    unsigned short pos = 0;
    while (*buf != '\0') {
        putchar_at(*buf, pos++);
        buf++;
    }
    return 0;
}

int putchar(char c)
{
    if (c == '\n') {
        cursor_move_newline();
        fb_check_scroll();
    } else if (c == '\t') {
        /* Advance to next 8-column tab stop */
        unsigned short col = (cursor_pos / 2) % FB_COLUMNS;
        unsigned short next_tab = (col + 8) & ~7u;
        if (next_tab > FB_COLUMNS) next_tab = FB_COLUMNS;
        while (col < next_tab) {
            fb_write_cell(cursor_pos, ' ', COLOR_BLACK, COLOR_WHITE);
            cursor_move_forward();
            col++;
        }
        fb_check_scroll();
    } else if (c == '\r') {
        /* Carriage return – move to start of current row */
        unsigned short row = (cursor_pos / 2) / FB_COLUMNS;
        cursor_pos = row * FB_COLUMNS * 2;
        fb_move_cursor(cursor_pos / 2);
    } else {
        fb_write_cell(cursor_pos, c, COLOR_BLACK, COLOR_WHITE);
        cursor_move_forward();
        fb_check_scroll();
    }
    return 0;
}

int puts(char *buf)
{
    while (*buf != '\0') {
        putchar(*buf);
        buf++;
    }
    return 0;
}

int write(char *buf, unsigned int len)
{
    for (unsigned int i = 0; i < len; i++) {
        putchar(buf[i]);
    }
    return 0;
}

int read_char_blocking(void)
{
    return keyboard_read_char_blocking();
}

int getchar(void)
{
#ifdef GUI_MODE
    /* In GUI mode delegate to the active terminal so that
     * gt_get_char() can repaint the compositor while waiting.       */
    {
        terminal_t *t = term_active();
        if (t && t->get_char && t->get_char != getchar) {
            return t->get_char();
        }
    }
#endif
    int ch = read_char_blocking();
    putchar((char)ch);
    return ch;
}

int readline(char *buf, unsigned int max_len)
{
    if (max_len == 0) {
        return 0;
    }

    unsigned int count = 0;
    while (1) {
        int ch = read_char_blocking();
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            putchar('\n');
            break;
        }
        if (ch == 8 || ch == '\b') {
            if (count > 0) {
                cursor_move_back();
                fb_write_cell(cursor_pos, ' ', COLOR_BLACK, COLOR_WHITE);
                fb_move_cursor(cursor_pos / 2);
                count--;
            }
            continue;
        }
        if (count + 1 < max_len) {
            buf[count++] = (char)ch;
            putchar((char)ch);
        }
    }

    buf[count] = '\0';
    return (int)count;
}

struct scan_state {
    int peek;
};

static void scan_init(struct scan_state *s)
{
    s->peek = -1;
}

static int scan_next(struct scan_state *s)
{
    if (s->peek != -1) {
        int c = s->peek;
        s->peek = -1;
        return c;
    }
    return read_char_blocking();
}

static void scan_push(struct scan_state *s, int ch)
{
    s->peek = ch;
}

static void scan_skip_space(struct scan_state *s)
{
    int ch;
    do {
        ch = scan_next(s);
    } while (is_space((char)ch));
    scan_push(s, ch);
}

static int scan_char_spec(struct scan_state *s, char *out)
{
    int ch = scan_next(s);
    *out = (char)ch;
    putchar((char)ch);
    return 1;
}

static int scan_string_spec(struct scan_state *s, char *out)
{
    scan_skip_space(s);
    int ch = scan_next(s);
    unsigned int idx = 0;
    while (!is_space((char)ch)) {
        out[idx++] = (char)ch;
        putchar((char)ch);
        ch = scan_next(s);
    }
    out[idx] = '\0';
    scan_push(s, ch);
    return 1;
}

static int scan_int_spec(struct scan_state *s, int *out)
{
    scan_skip_space(s);
    int ch = scan_next(s);
    int sign = 1;
    if (ch == '-') {
        sign = -1;
        putchar('-');
        ch = scan_next(s);
    }

    int value = 0;
    int digits = 0;
    while (ch >= '0' && ch <= '9') {
        value = value * 10 + (ch - '0');
        putchar((char)ch);
        ch = scan_next(s);
        digits++;
    }

    scan_push(s, ch);
    if (digits == 0) {
        return 0;
    }

    *out = value * sign;
    return 1;
}

int scanf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    int assigned = 0;
    struct scan_state s;
    scan_init(&s);

    while (*fmt) {
        if (is_space(*fmt)) {
            while (is_space(*fmt)) {
                fmt++;
            }
            scan_skip_space(&s);
            continue;
        }

        if (*fmt == '%') {
            fmt++;
            char spec = *fmt++;
            if (spec == 'c') {
                char *out = va_arg(ap, char *);
                assigned += scan_char_spec(&s, out);
            } else if (spec == 's') {
                char *out = va_arg(ap, char *);
                assigned += scan_string_spec(&s, out);
            } else if (spec == 'd') {
                int *out = va_arg(ap, int *);
                int ok = scan_int_spec(&s, out);
                if (!ok) {
                    break;
                }
                assigned += ok;
            } else {
                break;
            }
        } else {
            int ch = scan_next(&s);
            putchar((char)ch);
            if (ch != *fmt) {
                break;
            }
            fmt++;
        }
    }

    va_end(ap);
    return assigned;
}

/* =========================================================================
 * printf – formatted output to the VGA framebuffer.
 * Uses sprintf to a stack buffer, then puts().
 * ========================================================================= */
int printf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);

    /* Reuse sprintf internals (sprintf supports %d, %x, %s, %c, %%) */
    int len = 0;
    const char *p = fmt;
    char *dst = buf;

    while (*p && len < 510) {
        if (*p == '%') {
            p++;
            if (*p == 'd') {
                int val = va_arg(ap, int);
                char tmp[16];
                itoa(val, tmp, 10);
                for (int i = 0; tmp[i] && len < 510; i++)
                    dst[len++] = tmp[i];
            } else if (*p == 'u') {
                unsigned int val = va_arg(ap, unsigned int);
                char tmp[16];
                itoa((int)val, tmp, 10);
                for (int i = 0; tmp[i] && len < 510; i++)
                    dst[len++] = tmp[i];
            } else if (*p == 'x') {
                unsigned int val = va_arg(ap, unsigned int);
                char tmp[16];
                itoa((int)val, tmp, 16);
                for (int i = 0; tmp[i] && len < 510; i++)
                    dst[len++] = tmp[i];
            } else if (*p == 's') {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                while (*s && len < 510)
                    dst[len++] = *s++;
            } else if (*p == 'c') {
                char c = (char)va_arg(ap, int);
                dst[len++] = c;
            } else if (*p == '%') {
                dst[len++] = '%';
            }
            p++;
        } else {
            dst[len++] = *p++;
        }
    }
    dst[len] = '\0';

    va_end(ap);
    puts(buf);
    return len;
}

/* =========================================================================
 * putchar_color – write a single character with a specified foreground color.
 * Background is always black. Handles '\n'. Scrolls as needed.
 * ========================================================================= */
void putchar_color(char c, unsigned char fg)
{
    if (c == '\n') {
        cursor_move_newline();
        fb_check_scroll();
    } else if (c == '\t') {
        unsigned short col = (cursor_pos / 2) % FB_COLUMNS;
        unsigned short next_tab = (col + 8) & ~7u;
        if (next_tab > FB_COLUMNS) next_tab = FB_COLUMNS;
        while (col < next_tab) {
            fb_write_cell(cursor_pos, ' ', COLOR_BLACK, fg);
            cursor_move_forward();
            col++;
        }
        fb_check_scroll();
    } else if (c == '\r') {
        unsigned short row = (cursor_pos / 2) / FB_COLUMNS;
        cursor_pos = row * FB_COLUMNS * 2;
        fb_move_cursor(cursor_pos / 2);
    } else {
        fb_write_cell(cursor_pos, c, COLOR_BLACK, fg);
        cursor_move_forward();
        fb_check_scroll();
    }
}

/* =========================================================================
 * puts_color – write a string with a specified foreground color.
 * ========================================================================= */
void puts_color(const char *buf, unsigned char fg)
{
    while (*buf) {
        putchar_color(*buf, fg);
        buf++;
    }
}

/* =========================================================================
 * fb_get_cursor_pos – return current cursor_pos (byte offset).
 * ========================================================================= */
unsigned short fb_get_cursor_pos(void)
{
    return cursor_pos;
}
