#include "keyboard.h"
#include "pic.h"
#include "stdio.h"

/* -------------------------------------------------------------------------
 * Scan code set 1 → ASCII tables (unshifted and shifted)
 * ------------------------------------------------------------------------- */

/* Unshifted: Set 1 scancode to lowercase ASCII; 0 = unmapped. */
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

/* Shifted: uppercase letters + symbols on number row, etc. */
static const char scancode_ascii_shift[] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+', 8,
    '\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,
    'A','S','D','F','G','H','J','K','L',':','"','~',
    0,
    '|','Z','X','C','V','B','N','M','<','>','?',
    0,
    '*',
    0,
    ' ',
};

/* -------------------------------------------------------------------------
 * Modifier key scancodes (Set 1)
 * ------------------------------------------------------------------------- */
#define SC_LSHIFT_PRESS   0x2A
#define SC_LSHIFT_RELEASE 0xAA
#define SC_RSHIFT_PRESS   0x36
#define SC_RSHIFT_RELEASE 0xB6
#define SC_CAPSLOCK       0x3A

/* -------------------------------------------------------------------------
 * Modifier state
 * ------------------------------------------------------------------------- */
static int shift_held   = 0;   /* non-zero while either Shift is pressed */
static int caps_enabled = 0;   /* toggled by Caps Lock                   */

static unsigned char keyboard_read_scancode(void);
static char keyboard_scancode_to_ascii(unsigned char scancode, int shifted);

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

    /* ---- Track modifier key press / release ---- */
    if (scancode == SC_LSHIFT_PRESS || scancode == SC_RSHIFT_PRESS) {
        shift_held = 1;
        return;
    }
    if (scancode == SC_LSHIFT_RELEASE || scancode == SC_RSHIFT_RELEASE) {
        shift_held = 0;
        return;
    }
    if (scancode == SC_CAPSLOCK) {
        caps_enabled = !caps_enabled;
        return;
    }

    /* Ignore other key releases (high bit set) */
    if (scancode & 0x80) {
        return;
    }

    /*
     * Determine the effective shift state for this key:
     *   - Shift held  → use shifted table (uppercase + symbols)
     *   - Caps Lock   → invert case for letter keys only
     *   - Shift+Caps  → lowercase letters, shifted symbols
     */
    int use_shift = shift_held;
    char ascii = keyboard_scancode_to_ascii(scancode, use_shift);
    if (!ascii) {
        return;
    }

    /* Apply Caps Lock: if capslock is on and the char is a letter, toggle. */
    if (caps_enabled) {
        if (ascii >= 'a' && ascii <= 'z')      ascii -= 32;
        else if (ascii >= 'A' && ascii <= 'Z') ascii += 32;
    }

    buffer_push(ascii);
}

static unsigned char keyboard_read_scancode(void)
{
    return inb(KBD_DATA_PORT);
}

static char keyboard_scancode_to_ascii(unsigned char scancode, int shifted)
{
    const char *table = shifted ? scancode_ascii_shift : scancode_ascii;
    unsigned int table_size = shifted
        ? sizeof(scancode_ascii_shift)
        : sizeof(scancode_ascii);

    if (scancode < table_size) {
        return table[scancode];
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
