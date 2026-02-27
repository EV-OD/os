# Standard I/O — Framebuffer Console and Input

This document describes the kernel's standard I/O library, which provides a text console on the VGA framebuffer and keyboard-driven input routines. The implementation is in `c_files/src/stdio.c` with the API in `c_files/includes/stdio.h`. Low-level I/O port access (`outb`/`inb`) is implemented in `asm/stdio.s`.

For the hardware-level framebuffer format and color palette, see [framebuffer.md](../display/framebuffer.md).

## Framebuffer Addressing

The VGA text buffer is accessed at **0xC00B8000** (the standard 0xB8000 mapped into the higher-half kernel address space). The console is 80 columns × 25 rows. Each cell is 2 bytes: one for the ASCII character and one for the color attribute (foreground in bits 7–4, background in bits 3–0).

## Output Functions

### Cell-Level

```c
void fb_write_cell(unsigned int i, char c, unsigned char fg, unsigned char bg);
```
Writes character `c` at byte offset `i` in the framebuffer with the given foreground and background colors.

### Cursor Control

```c
void fb_move_cursor(unsigned short pos);  // Set hardware cursor position (cell index)
void fb_clear(void);                      // Clear entire screen (fill with spaces)
void cursor_move_home(void);              // Move cursor to position (0,0)
void cursor_move_newline(void);           // Advance to start of the next line
void cursor_move_back(void);              // Move cursor one cell left
void cursor_move_forward(void);           // Move cursor one cell right
```

The hardware cursor position is controlled via I/O ports 0x3D4/0x3D5 (command 14 = high byte, command 15 = low byte).

### Text Output

```c
int putchar(char c);                        // Print one character (handles '\n')
int puts(char *buf);                        // Print a NUL-terminated string
int write(char *buf, unsigned int len);     // Print exactly 'len' characters
int putchar_at(char c, unsigned short pos); // Print character at absolute cell position
int puts_at(char *buf);                     // Print string starting at cell 0
```

All text is rendered with black foreground on white background (`COLOR_BLACK` / `COLOR_WHITE`).

## Input Functions

Input is driven by the keyboard ring buffer (see [keyboard.md](../drivers/keyboard.md)).

### Single Character

```c
int getchar(void);
```
Blocks until a key is pressed, echoes it to the framebuffer, and returns the ASCII value.

### Line Input

```c
int readline(char *buf, unsigned int max_len);
```
Reads a line of input into `buf`:
- Echoes typed characters to the framebuffer.
- Handles **backspace** (erases the last character visually and in the buffer).
- Ignores carriage return (`\r`).
- Returns on **Enter** (`\n`); NUL-terminates the buffer.
- Returns the number of characters read (excluding the terminator).

### Formatted Input

```c
int scanf(const char *fmt, ...);
```
A simplified `scanf` supporting three format specifiers:

| Specifier | Argument | Behavior |
|-----------|----------|----------|
| `%c` | `char *` | Reads one character |
| `%s` | `char *` | Reads a whitespace-delimited word |
| `%d` | `int *` | Reads a signed decimal integer |

Features:
- **Whitespace skipping** before `%s` and `%d` (matches any run of spaces/tabs/newlines).
- **Negative integers**: handles a leading `-` sign for `%d`.
- **Echo**: every consumed character is echoed to the framebuffer.
- Returns the number of successfully assigned fields.

The implementation uses a `scan_state` struct with a one-character pushback buffer, consuming input from the keyboard one character at a time.

## Color Constants

Defined in `stdio.h`:

| Name | Value | Name | Value |
|------|-------|------|-------|
| `COLOR_BLACK` | 0x00 | `COLOR_DARK_GREY` | 0x08 |
| `COLOR_BLUE` | 0x01 | `COLOR_LIGHT_BLUE` | 0x09 |
| `COLOR_GREEN` | 0x02 | `COLOR_LIGHT_GREEN` | 0x0A |
| `COLOR_CYAN` | 0x03 | `COLOR_LIGHT_CYAN` | 0x0B |
| `COLOR_RED` | 0x04 | `COLOR_LIGHT_RED` | 0x0C |
| `COLOR_MAGENTA` | 0x05 | `COLOR_LIGHT_MAGENTA` | 0x0D |
| `COLOR_BROWN` | 0x06 | `COLOR_LIGHT_BROWN` | 0x0E |
| `COLOR_LIGHT_GREY` | 0x07 | `COLOR_WHITE` | 0x0F |

## Port I/O

The assembly file `asm/stdio.s` exports two functions used throughout the kernel:

```c
void outb(unsigned short port, unsigned char data);  // Write byte to I/O port
unsigned char inb(unsigned short port);               // Read byte from I/O port
```

These are the fundamental building blocks for all hardware communication (serial port, PIC, framebuffer cursor, keyboard).
