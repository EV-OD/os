# Keyboard Interrupt Handler

This module handles PS/2 keyboard input (scan code set 1) by translating scancodes to ASCII and printing them to both serial and framebuffer output.

- API: [c_files/includes/keyboard.h](c_files/includes/keyboard.h)
- Implementation: [c_files/src/keyboard.c](c_files/src/keyboard.c)

## Flow

1. `keyboard_init()` registers the handler for IRQ1 (vector 33 after PIC remap) and unmasks IRQ1 via `pic_clear_mask(1)`.
2. On each interrupt, the ISR reads the scancode from port 0x60.
3. Key releases (scancode with high bit set) are ignored; presses are mapped to ASCII using a simple set-1 table.
4. A small line buffer (128 bytes) handles basic editing:
	- Backspace erases the last character on both serial and framebuffer.
	- Enter emits a newline and clears the buffer.
	- Printable characters are appended and echoed to serial (`serial_write_char`) and framebuffer (`putchar`).

## Notes

- Only a subset of scancodes is mapped; unmapped keys return 0 and are ignored.
- Enter is translated to `\n`; Backspace maps to ASCII 8, and Tab to `\t`.
- The common dispatcher in [c_files/src/isr.c](c_files/src/isr.c) sends EOIs to the PIC after the handler returns.
