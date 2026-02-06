#include "stdio.h"

volatile unsigned char *framebuffer = (unsigned char *) 0x000B8000;
static unsigned short cursor_pos = 0;

void fb_write_cell(unsigned int i, char c, unsigned char fg, unsigned char bg)
{
    framebuffer[i] = c;
    framebuffer[i + 1] = ((fg & 0x0F) << 4) | (bg & 0x0F);
}

void fb_move_cursor(unsigned short pos)
{
    outb(FB_COMMAND_PORT, FB_HIGH_BYTE_COMMAND);
    outb(FB_DATA_PORT,    ((pos >> 8) & 0x00FF));
    outb(FB_COMMAND_PORT, FB_LOW_BYTE_COMMAND);
    outb(FB_DATA_PORT,    pos & 0x00FF);
    cursor_pos = pos * 2;
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

int putchar_at(char c, unsigned short pos)
{
    fb_write_cell(pos, c, COLOR_BLACK, COLOR_WHITE);
    return 0;
}

int puts_at(char *buf)
{
    unsigned int i = 0;
    while (*buf != '\0') {
        putchar_at(*buf, i * 2);
        buf++;
        i++;
    }
    return 0;
}

int write(char *buf, unsigned int len)
{
    for (unsigned int i = 0; i < len; i++) {
        putchar_at(buf[i], i * 2);
    }
    return len;
}

void fb_clear(void)
{
    for (unsigned int i = 0; i < FB_COLUMNS * FB_ROWS * 2; i += 2) {
        framebuffer[i] = FB_EMPTY_CELL;
        framebuffer[i + 1] = FB_DEFAULT_COLOR;
    }
    cursor_move_home();
}

