#include "keyboard.h"
#include "pic.h"
#include "stdio.h"
#include "sched.h"
#include "process.h"
#include "log.h"

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
#define SC_LCTRL_PRESS    0x1D  /* Left Ctrl pressed  */
#define SC_LCTRL_RELEASE  0x9D  /* Left Ctrl released */
#define SC_C_KEY          0x2E  /* 'c' key scancode   */
#define SC_S_KEY          0x1F  /* 's' key scancode   */
#define SC_Q_KEY          0x10  /* 'q' key scancode   */
#define SC_E0_PREFIX      0xE0  /* extended scancode prefix  */
#define SC_UP_KEY         0x48  /* arrow up (after E0)       */
#define SC_DOWN_KEY       0x50  /* arrow down (after E0)     */
#define SC_LEFT_KEY       0x4B  /* arrow left (after E0)     */
#define SC_RIGHT_KEY      0x4D  /* arrow right (after E0)    */
#define SC_HOME_KEY       0x47  /* Home (after E0)           */
#define SC_END_KEY        0x4F  /* End (after E0)            */

/* Virtual key codes pushed to the buffer for special keys */
#define KEY_UP    200u
#define KEY_DOWN  201u
#define KEY_LEFT  202u
#define KEY_RIGHT 203u
#define KEY_HOME  204u
#define KEY_END   205u

/* -------------------------------------------------------------------------
 * Modifier state
 * ------------------------------------------------------------------------- */
static int shift_held   = 0;   /* non-zero while either Shift is pressed */
static int caps_enabled = 0;   /* toggled by Caps Lock                   */
static int ctrl_held    = 0;   /* non-zero while Ctrl is pressed          */
static int e0_pending   = 0;   /* non-zero: last byte was E0 prefix       */

static unsigned char keyboard_read_scancode(void);
static char keyboard_scancode_to_ascii(unsigned char scancode, int shifted);

/* Simple ring buffer – unsigned so arrow-key codes 200-205 survive intact.
 * head/tail/char_buffer MUST be volatile: the ISR modifies them
 * asynchronously and the main-loop code must re-read from memory every
 * time (no cached-register optimisation). */
static volatile unsigned char char_buffer[128];
static volatile unsigned int head = 0;
static volatile unsigned int tail = 0;

static int buffer_is_full(void)
{
    return ((head + 1) % sizeof(char_buffer)) == tail;
}

static int buffer_is_empty(void)
{
    return head == tail;
}

static void buffer_push(unsigned char c)
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
    log_info("[kbd-pop] char=%d ('%c')  head=%d new_tail=%d",
             c, (c >= 32 && c < 127) ? (char)c : '?',
             (int)head, (int)tail);
    return c;
}

static void keyboard_isr(struct cpu_state *cpu, struct stack_state *stack, unsigned int interrupt)
{
    (void)cpu;
    (void)stack;
    (void)interrupt;

    unsigned char scancode = keyboard_read_scancode();

    /* ---- Extended (E0-prefix) scancode handling ---- */
    if (scancode == SC_E0_PREFIX) {
        e0_pending = 1;
        return;
    }
    if (e0_pending) {
        e0_pending = 0;
        switch (scancode) {
            case SC_UP_KEY:    buffer_push(KEY_UP);    break;
            case SC_DOWN_KEY:  buffer_push(KEY_DOWN);  break;
            case SC_LEFT_KEY:  buffer_push(KEY_LEFT);  break;
            case SC_RIGHT_KEY: buffer_push(KEY_RIGHT); break;
            case SC_HOME_KEY:  buffer_push(KEY_HOME);  break;
            case SC_END_KEY:   buffer_push(KEY_END);   break;
            /* Right Ctrl sends E0 + 0x1D (press) / E0 + 0x9D (release) */
            case SC_LCTRL_PRESS:
                log_info("[kbd] Right Ctrl pressed");
                ctrl_held = 1;
                return;
            case SC_LCTRL_RELEASE:
                log_info("[kbd] Right Ctrl released");
                ctrl_held = 0;
                return;
            default: break;
        }
        sched_wake_waiters(WAIT_KEY);
        return;
    }

    /* ---- Track modifier key press / release (with serial debug) ---- */
    if (scancode == SC_LSHIFT_PRESS || scancode == SC_RSHIFT_PRESS) {
        log_info("[kbd] Shift pressed (sc=0x%x)", (int)scancode);
        shift_held = 1;
        return;
    }
    if (scancode == SC_LSHIFT_RELEASE || scancode == SC_RSHIFT_RELEASE) {
        shift_held = 0;
        return;
    }
    if (scancode == SC_LCTRL_PRESS) {
        log_info("[kbd] Left Ctrl pressed");
        ctrl_held = 1;
        return;
    }
    if (scancode == SC_LCTRL_RELEASE) {
        log_info("[kbd] Left Ctrl released");
        ctrl_held = 0;
        return;
    }
    if (scancode == SC_CAPSLOCK) {
        caps_enabled = !caps_enabled;
        return;
    }

    /* Log every non-modifier scancode we see (press + release) */
    log_info("[kbd-isr] sc=0x%02x  shift=%d ctrl=%d",
             (int)scancode, (int)shift_held, (int)ctrl_held);

    /* Ignore other key releases (high bit set) */
    if (scancode & 0x80) {
        log_info("[kbd-isr] sc=0x%02x → release, ignored", (int)scancode);
        return;
    }

    /* ---- Ctrl+C: signal the current process to terminate ---- */
    if (ctrl_held && scancode == SC_C_KEY) {
        process_t *cur = sched_current();
        if (cur && cur->state != PROC_DEAD) {
            cur->killed = 1;
            sched_wake_process(cur);
        }
        return;
    }
    /* Ctrl+key combos – log to serial for debugging */
    if (ctrl_held) {
        char _ascii = keyboard_scancode_to_ascii(scancode, 0);
        if (_ascii >= 'a' && _ascii <= 'z')
            log_info("[kbd] Ctrl+%c (sc=0x%x)", (int)_ascii, (int)scancode);
        else
            log_info("[kbd] Ctrl+sc=0x%x", (int)scancode);
    }
    /* Ctrl+S → 19 (DC3), Ctrl+Q → 17 (DC1): pass to app for editor shortcuts */
    if (ctrl_held && scancode == SC_S_KEY) { log_info("[kbd] Ctrl+S → pushing 19 (save)"); buffer_push(19); sched_wake_waiters(WAIT_KEY); return; }
    if (ctrl_held && scancode == SC_Q_KEY) { log_info("[kbd] Ctrl+Q → pushing 17 (quit)"); buffer_push(17); sched_wake_waiters(WAIT_KEY); return; }

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

    log_info("[kbd-isr] ascii=%d ('%c') pushed  head=%d tail=%d",
             (int)(unsigned char)ascii, (ascii >= 32 && ascii < 127) ? ascii : '?',
             (int)head, (int)tail);
    buffer_push(ascii);
    log_info("[kbd-isr] after push  head=%d tail=%d", (int)head, (int)tail);
    /* Wake any process sleeping in sys_read / sys_gui_wait. */
    sched_wake_waiters(WAIT_KEY);
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

void keyboard_flush(void)
{
    /* Drain all pending chars from the ring buffer so that PS/2 init
     * residue (mouse ACK bytes, leftover scancodes) cannot appear as
     * phantom keystrokes after the scheduler starts.                 */
    while (keyboard_available())
        keyboard_read_char();
}
