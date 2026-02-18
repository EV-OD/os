#include "keyboard.h"
#include "stdio.h"
#include "serial.h"
#include "pic.h"

/* Set 1 scancode to ASCII for main keys; 0 means unmapped. */
static const char scancode_ascii[] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', 8, /* Backspace */
    '\t', /* Tab */
    'q','w','e','r','t','y','u','i','o','p','[',']','\n', /* Enter */
    0, /* Control */
    'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, /* Left Shift */
    '\\','z','x','c','v','b','n','m',',','.','/',
    0, /* Right Shift */
    '*',
    0, /* Alt */
    ' ', /* Space */
};

static void keyboard_isr(struct cpu_state *cpu, struct stack_state *stack, unsigned int interrupt)
{
    (void)cpu;
    (void)stack;
    (void)interrupt;

    unsigned char scancode = keyboard_read_scancode();

    /* Ignore key releases (high bit set) */
    // 0x80 is the high bit of the scancode, which indicates a key release event. If this bit is set, we ignore the scancode because we only want to process key press events. This allows us to avoid generating duplicate input for key releases and simplifies the handling of keyboard input in the rest of the system.
    if (scancode & 0x80) {
        return;
    }

    char ascii = keyboard_scancode_to_ascii(scancode);
    if (ascii) {
        serial_write_char(ascii);
        putchar(ascii);
    }
}

unsigned char keyboard_read_scancode(void)
{
    return inb(KBD_DATA_PORT);
}

char keyboard_scancode_to_ascii(unsigned char scancode)
{
    if (scancode < sizeof(scancode_ascii)) {
        return scancode_ascii[scancode];
    }
    return 0;
}

void keyboard_init(void)
{
    register_interrupt_handler(33, keyboard_isr);
    pic_clear_mask(1); /* Unmask keyboard IRQ */
}
