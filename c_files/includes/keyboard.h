#ifndef INCLUDE_KEYBOARD_H
#define INCLUDE_KEYBOARD_H

#include "isr.h"

#define KBD_DATA_PORT 0x60

void keyboard_init(void);

/* Non-blocking: returns 1 if a character is queued, 0 otherwise. */
int keyboard_available(void);

/* Non-blocking: returns next char if available, otherwise -1. */
int keyboard_read_char(void);

/* Blocking: waits until a char is available, then returns it. */
int keyboard_read_char_blocking(void);

#endif /* INCLUDE_KEYBOARD_H */
