#ifndef INCLUDE_STDIO_H
#define INCLUDE_STDIO_H

/* I/O Ports */
#define FB_COMMAND_PORT         0x3D4
#define FB_DATA_PORT            0x3D5

/* I/O Port Commands */
#define FB_HIGH_BYTE_COMMAND    14
#define FB_LOW_BYTE_COMMAND     15

/* VGA Colors */
#define COLOR_BLACK             0x00
#define COLOR_BLUE              0x01
#define COLOR_GREEN             0x02
#define COLOR_CYAN              0x03
#define COLOR_RED               0x04
#define COLOR_MAGENTA           0x05
#define COLOR_BROWN             0x06
#define COLOR_LIGHT_GREY        0x07
#define COLOR_DARK_GREY         0x08
#define COLOR_LIGHT_BLUE        0x09
#define COLOR_LIGHT_GREEN       0x0A
#define COLOR_LIGHT_CYAN        0x0B
#define COLOR_LIGHT_RED         0x0C
#define COLOR_LIGHT_MAGENTA     0x0D
#define COLOR_LIGHT_BROWN       0x0E
#define COLOR_WHITE             0x0F

/* Framebuffer Constants */
#define FB_DEFAULT_COLOR        0x07
#define FB_EMPTY_CELL           0x20
#define FB_COLUMNS              80
#define FB_ROWS                 25

/* Assembly Helper */
void outb(unsigned short port, unsigned char data);

/* Framebuffer Functions */
void fb_write_cell(unsigned int i, char c, unsigned char fg, unsigned char bg);
void fb_move_cursor(unsigned short pos);
void fb_clear(void);

/* Cursor Functions */
void cursor_move_home(void);
void cursor_move_newline(void);
void cursor_move_back(void);
void cursor_move_forward(void);

/* Printing Functions */
int putchar_at(char c, unsigned short pos);
int puts_at(char *buf);
int putchar(char c);
int puts(char *buf);
int write(char *buf, unsigned int len);

extern volatile unsigned char *framebuffer;

#endif /* INCLUDE_STDIO_H */
