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

/* Flush (discard) all pending characters in the ring buffer.
 * Call before starting the scheduler to discard PS/2 init residue. */
void keyboard_flush(void);

#endif /* INCLUDE_KEYBOARD_H */
