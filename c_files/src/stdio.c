#include "stdio.h"
#include "keyboard.h"
#include "string.h"

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)

volatile unsigned char *framebuffer = (unsigned char *)0x000B8000;
static unsigned short cursor_pos = 0;

static int is_space(char c)
{
    return c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
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
    cursor_move_home();
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
    } else {
        fb_write_cell(cursor_pos, c, COLOR_BLACK, COLOR_WHITE);
        cursor_move_forward();
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

static int read_char_blocking(void)
{
    return keyboard_read_char_blocking();
}

int getchar(void)
{
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
