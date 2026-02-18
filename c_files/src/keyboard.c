#include "keyboard.h"
#include "pic.h"
#include "stdio.h"

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

static unsigned char keyboard_read_scancode(void);
static char keyboard_scancode_to_ascii(unsigned char scancode);

/* Simple ring buffer for ASCII keypresses. */
static char char_buffer[128];
static unsigned int head = 0;
static unsigned int tail = 0;

static int buffer_is_full(void)
{
    return ((head + 1) % sizeof(char_buffer)) == tail;
}

static int buffer_is_empty(void)
{
    return head == tail;
}

static void buffer_push(char c)
{
    if (buffer_is_full()) {
        return; /* drop if full */
    }
    char_buffer[head] = c;
    head = (head + 1) % sizeof(char_buffer);
}

static int buffer_pop(void)
{
    if (buffer_is_empty()) {
        return -1;
    }
    int c = (int)char_buffer[tail];
    tail = (tail + 1) % sizeof(char_buffer);
    return c;
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

    buffer_push(ascii);
}

static unsigned char keyboard_read_scancode(void)
{
    return inb(KBD_DATA_PORT);
}

static char keyboard_scancode_to_ascii(unsigned char scancode)
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

int keyboard_available(void)
{
    return !buffer_is_empty();
}

int keyboard_read_char(void)
{
    return buffer_pop();
}

int keyboard_read_char_blocking(void)
{
    int c;
    while ((c = buffer_pop()) == -1) {
        /* spin */
    }
    return c;
}
