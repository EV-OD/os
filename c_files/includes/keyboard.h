#ifndef INCLUDE_KEYBOARD_H
#define INCLUDE_KEYBOARD_H

#include "isr.h"

#define KBD_DATA_PORT 0x60

void keyboard_init(void);
unsigned char keyboard_read_scancode(void);
char keyboard_scancode_to_ascii(unsigned char scancode);
void keyboard_clear_buffer(void);
const char *keyboard_get_buffer(void);

#endif /* INCLUDE_KEYBOARD_H */
