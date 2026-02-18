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

static char line_buffer[128];
static unsigned int line_pos = 0;

const char *keyboard_get_buffer(void)
{
    line_buffer[line_pos] = '\0';
    return line_buffer;
}

void keyboard_clear_buffer(void)
{
    line_pos = 0;
}

static void keyboard_isr(struct cpu_state *cpu, struct stack_state *stack, unsigned int interrupt)
{
    (void)cpu;
    (void)stack;
    (void)interrupt;

    unsigned char scancode = keyboard_read_scancode();

    /* Ignore key releases (high bit set) */
    if (scancode & 0x80) {
        return;
    }

    char ascii = keyboard_scancode_to_ascii(scancode);
    if (!ascii) {
        return;
    }

    if (ascii == '\b') {
        if (line_pos > 0) {
            line_pos--;
            serial_write_char('\b');
            serial_write_char(' ');
            serial_write_char('\b');
            cursor_move_back();
            putchar(' ');
            cursor_move_back();
        }
        return;
    }

    /* Enter: echo newline and reset buffer */
    if (ascii == '\n' || ascii == '\r') {
        line_buffer[line_pos] = '\0';
        serial_write_char('\n');
        putchar('\n');
        line_pos = 0;
        return;
    }

    if (line_pos + 1 < sizeof(line_buffer)) {
        line_buffer[line_pos++] = ascii;
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
