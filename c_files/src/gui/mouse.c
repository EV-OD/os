/* =========================================================================
 * gui/mouse.c – PS/2 mouse driver (IRQ12, standard 3-byte packet protocol)
 *
 * ── PS/2 Architecture ────────────────────────────────────────────────────
 * The Intel 8042 PS/2 controller manages both the keyboard (port 1) and
 * the auxiliary (mouse) channel (port 2) on a single shared data port 0x60.
 * The status register (port 0x64) contains two key flags:
 *
 *   Bit 0 (OBF)   – Output Buffer Full: data is waiting at port 0x60.
 *   Bit 5 (AUXB)  – If set, the byte in the output buffer is from the
 *                   mouse (aux port), NOT from the keyboard.
 *
 * ── Interrupt Sequence ───────────────────────────────────────────────────
 * IRQ12 fires whenever the 8042 places a mouse byte in the output buffer.
 * We MUST check AUXB before reading, otherwise keyboard scan codes will
 * be consumed here instead of by the keyboard driver, causing both drivers
 * to receive corrupt data.
 *
 * ── Packet Format (3 bytes) ──────────────────────────────────────────────
 * Byte 0:  7      6      5      4      3      2      1      0
 *          YOVF   XOVF  YSIGN  XSIGN  ALWAY1  MID    RIGHT  LEFT
 * Byte 1:  X movement delta (lower 8 bits; sign in byte0 bit 4)
 * Byte 2:  Y movement delta (lower 8 bits; sign in byte0 bit 5)
 *          PS/2 Y is positive-up; screen Y is positive-down → negate.
 *
 * ── Initialisation Order ─────────────────────────────────────────────────
 * Register the IRQ LAST – after the init ACK / self-test bytes have been
 * explicitly drained.  Reversed order causes those bytes to corrupt the
 * packet accumulator and the cursor jumps to a corner and freezes.
 * ========================================================================= */

#include "gui/mouse.h"
#include "gui/fb.h"
#include "gui/gfx.h"
#include "gui/color.h"
#include "isr.h"
#include "pic.h"
#include "stdio.h"   /* outb / inb */
#include "log.h"

/* =========================================================================
 * 8042 / PS/2 constants
 * ========================================================================= */

#define PS2_DATA            0x60
#define PS2_STATUS          0x64   /* read  */
#define PS2_CMD             0x64   /* write */

/* Status register bits */
#define PS2_SR_OBF          0x01   /* Output buffer full – byte ready at 0x60 */
#define PS2_SR_IBF          0x02   /* Input  buffer full – controller busy     */
#define PS2_SR_AUXB         0x20   /* 1 = OBF byte came from mouse (aux port)  */

/* Controller commands */
#define PS2_CTL_READ_CFG    0x20
#define PS2_CTL_WRITE_CFG   0x60
#define PS2_CTL_DISABLE_AUX 0xA7
#define PS2_CTL_ENABLE_AUX  0xA8
#define PS2_CTL_DISABLE_KBD 0xAD
#define PS2_CTL_ENABLE_KBD  0xAE
#define PS2_CTL_WRITE_AUX   0xD4   /* Route next byte to mouse */

/* Configuration byte bits */
#define PS2_CFG_IRQ12       0x02   /* Enable mouse IRQ12   */
#define PS2_CFG_AUX_CLOCK   0x20   /* 0 = mouse clock ON   */

/* Mouse device commands */
#define MS_RESET            0xFF
#define MS_SET_DEFAULTS     0xF6
#define MS_ENABLE           0xF4
#define MS_ACK              0xFA
#define MS_SELF_TEST_OK     0xAA

/* IRQ */
#define MOUSE_IRQ_LINE      12
#define MOUSE_IRQ_VECTOR    44     /* PIC2 base 0x28 + line 4 = 44 */

/* Packet byte-0 flags */
#define PKT_ALWAYS1         0x08
#define PKT_XSIGN           0x10
#define PKT_YSIGN           0x20
#define PKT_XOVF            0x40
#define PKT_YOVF            0x80

#define PS2_TIMEOUT         100000u

/* =========================================================================
 * Driver state
 * ========================================================================= */

static mouse_state_t s_mouse  = {0, 0, 0};
static int           s_ready  = 0;

/* 3-byte packet accumulator */
static unsigned char s_pkt[3];
static int           s_pkt_idx = 0;

/* ── Cursor sprite (12 × 19) ────────────────────────────────────────────
 * 0 = black outline, 1 = white fill, 2 = transparent                   */
#define CUR_W  12
#define CUR_H  19

static const unsigned char CURSOR[CUR_H][CUR_W] = {
    {1,2,2,2,2,2,2,2,2,2,2,2},
    {1,1,2,2,2,2,2,2,2,2,2,2},
    {1,0,1,2,2,2,2,2,2,2,2,2},
    {1,0,0,1,2,2,2,2,2,2,2,2},
    {1,0,0,0,1,2,2,2,2,2,2,2},
    {1,0,0,0,0,1,2,2,2,2,2,2},
    {1,0,0,0,0,0,1,2,2,2,2,2},
    {1,0,0,0,0,0,0,1,2,2,2,2},
    {1,0,0,0,0,0,0,0,1,2,2,2},
    {1,0,0,0,0,0,0,0,0,1,2,2},
    {1,0,0,0,0,1,1,1,1,1,2,2},
    {1,0,0,1,0,0,1,2,2,2,2,2},
    {1,0,1,2,1,0,0,1,2,2,2,2},
    {1,1,2,2,2,1,0,0,1,2,2,2},
    {1,2,2,2,2,2,1,0,0,1,2,2},
    {2,2,2,2,2,2,2,1,0,0,1,2},
    {2,2,2,2,2,2,2,2,1,0,0,1},
    {2,2,2,2,2,2,2,2,2,1,0,1},
    {2,2,2,2,2,2,2,2,2,2,1,2},
};

#define CURSOR_FG   COLOR_WHITE
#define CURSOR_OUT  COLOR_RGB(0x00, 0x00, 0x00)

/* Save-under buffer */
static unsigned int s_saved[CUR_W * CUR_H];
static int          s_drawn  = 0;
static int          s_prev_x = 0;
static int          s_prev_y = 0;

/* =========================================================================
 * Low-level 8042 helpers
 * ========================================================================= */

/** Block until the input buffer is empty (safe to write command/data). */
static void ps2_wait_write(void)
{
    unsigned int t = PS2_TIMEOUT;
    while (t-- && (inb(PS2_STATUS) & PS2_SR_IBF)) {}
}

/** Block until the output buffer has a byte ready to read. */
static void ps2_wait_read(void)
{
    unsigned int t = PS2_TIMEOUT;
    while (t-- && !(inb(PS2_STATUS) & PS2_SR_OBF)) {}
}

/**
 * Drain every byte currently in the 8042 output buffer and discard them.
 * Called before and after mouse init to prevent ACK / self-test bytes from
 * leaking into the packet accumulator once the IRQ handler is installed.
 */
static void ps2_flush(void)
{
    unsigned int t = 32;
    while (t-- && (inb(PS2_STATUS) & PS2_SR_OBF))
        (void)inb(PS2_DATA);
}

/** Issue a command to the PS/2 controller. */
static void ctl_cmd(unsigned char cmd)
{
    ps2_wait_write();
    outb(PS2_CMD, cmd);
}

/**
 * Send a byte to the mouse via the PS2_CTL_WRITE_AUX tunnel.
 * Every byte sent to the mouse arrives as an ACK (0xFA) or error reply
 * in the output buffer; the caller must read those with ps2_flush() or
 * explicit ps2_wait_read() + inb() calls.
 */
static void mouse_send(unsigned char byte)
{
    ctl_cmd(PS2_CTL_WRITE_AUX);
    ps2_wait_write();
    outb(PS2_DATA, byte);
}

/**
 * Send a command to the mouse and poll for the ACK byte (0xFA).
 * Returns 1 on success, 0 if ACK is not received.
 */
static int mouse_cmd_ack(unsigned char cmd)
{
    unsigned int t;
    mouse_send(cmd);
    for (t = 0; t < 1000u; t++) {
        ps2_wait_read();
        if (inb(PS2_DATA) == MS_ACK) return 1;
    }
    return 0;
}

/* =========================================================================
 * Packet processing
 * ========================================================================= */

static int s_clamp(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/**
 * Decode a complete 3-byte PS/2 mouse packet and update the mouse state.
 *
 * 9-bit signed X/Y:
 *   The data byte holds the low 8 bits; the sign bit is in byte 0.
 *   Negative values: data - 256  (equivalent to sign-extending bit 8).
 *   Example: if XSIGN=1 and p[1]=0xFE → dx = 0xFE - 256 = -2.
 */
static void process_packet(unsigned char *p)
{
    int dx, dy;

    /* Bit 3 must always be 1 – if zero we are de-synced; drop & resync. */
    if (!(p[0] & PKT_ALWAYS1)) {
        s_pkt_idx = 0;
        return;
    }

    /* Overflow means the delta is unreliable – discard the packet. */
    if (p[0] & (PKT_XOVF | PKT_YOVF))
        return;

    dx = (int)(unsigned int)p[1];
    if (p[0] & PKT_XSIGN) dx -= 256;

    dy = (int)(unsigned int)p[2];
    if (p[0] & PKT_YSIGN) dy -= 256;
    dy = -dy;   /* PS/2 Y positive = up; screen Y positive = down */

    s_mouse.x       = s_clamp(s_mouse.x + dx, 0, (int)fb_width()  - 1);
    s_mouse.y       = s_clamp(s_mouse.y + dy, 0, (int)fb_height() - 1);
    s_mouse.buttons = (unsigned char)(p[0] & 0x07);
}

/* =========================================================================
 * IRQ 12 handler
 * ========================================================================= */

/**
 * Called on every IRQ12.
 *
 * Critical checks before consuming the byte:
 *   1. OBF  must be set – data is actually present in the buffer.
 *   2. AUXB must be set – the byte came from the mouse, not the keyboard.
 *
 * If AUXB is clear the byte is keyboard data and belongs to the keyboard
 * driver's IRQ1 handler; we must NOT read it here.
 *
 * For the first byte of a new packet (s_pkt_idx == 0) we additionally
 * verify that PKT_ALWAYS1 is set.  If it is not, the byte is not a valid
 * packet start (could be a stale ACK or a stream mis-alignment) and we
 * discard it, effectively waiting for a clean packet boundary.
 */
static void mouse_irq_handler(struct cpu_state   *cpu,
                               struct stack_state *stack,
                               unsigned int        interrupt)
{
    unsigned char status;
    unsigned char byte;

    (void)cpu; (void)stack; (void)interrupt;

    status = inb(PS2_STATUS);

    if (!(status & PS2_SR_OBF))   return;  /* nothing to read        */
    if (!(status & PS2_SR_AUXB))  return;  /* not from mouse         */

    byte = inb(PS2_DATA);

    /* At packet position 0, enforce the ALWAYS1 synchronisation bit. */
    if (s_pkt_idx == 0 && !(byte & PKT_ALWAYS1))
        return;  /* discard and wait for a real first byte */

    s_pkt[s_pkt_idx++] = byte;

    if (s_pkt_idx == 3) {
        s_pkt_idx = 0;
        process_packet(s_pkt);
    }
}

/* =========================================================================
 * mouse_init
 * ========================================================================= */

void mouse_init(void)
{
    unsigned char cfg;

    /* ── 1. Disable both ports – no interference during init ─────────── */
    ctl_cmd(PS2_CTL_DISABLE_KBD);
    ctl_cmd(PS2_CTL_DISABLE_AUX);

    /* ── 2. Flush stale bytes from the output buffer ─────────────────── */
    ps2_flush();

    /* ── 3. Configure the controller ─────────────────────────────────── */
    ctl_cmd(PS2_CTL_READ_CFG);
    ps2_wait_read();
    cfg = inb(PS2_DATA);

    cfg |=  PS2_CFG_IRQ12;     /* Enable mouse IRQ12                       */
    cfg &= ~PS2_CFG_AUX_CLOCK; /* Clear bit 5: enable mouse clock          */

    ctl_cmd(PS2_CTL_WRITE_CFG);
    ps2_wait_write();
    outb(PS2_DATA, cfg);

    /* ── 4. Enable the auxiliary (mouse) port ────────────────────────── */
    ctl_cmd(PS2_CTL_ENABLE_AUX);

    /* ── 5. Reset the mouse and read back the self-test response ─────── */
    mouse_send(MS_RESET);
    ps2_wait_read(); (void)inb(PS2_DATA);  /* 0xFA ACK         */
    ps2_wait_read(); (void)inb(PS2_DATA);  /* 0xAA self-test   */
    ps2_wait_read(); (void)inb(PS2_DATA);  /* 0x00 device ID   */

    /* ── 6. Restore defaults ─────────────────────────────────────────── */
    if (!mouse_cmd_ack(MS_SET_DEFAULTS))
        log_warning("[mouse] Set Defaults NAK");

    /* ── 7. Enable data reporting ────────────────────────────────────── */
    if (!mouse_cmd_ack(MS_ENABLE))
        log_warning("[mouse] Enable Reporting NAK");

    /* ── 8. Drain ALL ACK / self-test residue before installing handler ─
     *
     * This is the most important flush.  Without it the ACK bytes from
     * steps 5-7 are the FIRST bytes seen by mouse_irq_handler, causing
     * the packet accumulator to start at the wrong position.  This makes
     * the cursor jump to a screen corner and ignore all real movement.  */
    ps2_flush();

    /* ── 9. Re-enable keyboard ───────────────────────────────────────── */
    ctl_cmd(PS2_CTL_ENABLE_KBD);

    /* ── 10. Install IRQ12 handler (LAST – after all init traffic) ────── */
    register_interrupt_handler(MOUSE_IRQ_VECTOR, mouse_irq_handler);
    pic_clear_mask(MOUSE_IRQ_LINE);

    /* Cursor starts at screen centre */
    s_mouse.x       = (int)(fb_width()  / 2);
    s_mouse.y       = (int)(fb_height() / 2);
    s_mouse.buttons = 0;
    s_pkt_idx       = 0;
    s_ready         = 1;

    log_info("[mouse] PS/2 mouse ready – IRQ12 active, cursor at (%d,%d)",
             s_mouse.x, s_mouse.y);
}

/* =========================================================================
 * Public accessors
 * ========================================================================= */

mouse_state_t mouse_get(void)       { return s_mouse; }
int           mouse_available(void) { return s_ready;  }

/* =========================================================================
 * mouse_draw_cursor
 *
 * Draws a hardware-style software cursor using a save-under technique:
 *   1. Restore pixels that were behind the cursor last frame.
 *   2. Save pixels behind the cursor's new position.
 *   3. Stamp the arrow sprite.
 *
 * Must be called once per frame AFTER wm_paint_all() so the cursor
 * is always composited on top of every window.
 * ========================================================================= */

void mouse_draw_cursor(void)
{
    int row, col;

    if (!s_ready || !fb_available()) return;

    /* Restore previous save-under */
    if (s_drawn) {
        for (row = 0; row < CUR_H; row++)
            for (col = 0; col < CUR_W; col++)
                fb_put_pixel(s_prev_x + col,
                             s_prev_y + row,
                             s_saved[row * CUR_W + col]);
    }

    /* Save what is currently behind the cursor */
    for (row = 0; row < CUR_H; row++)
        for (col = 0; col < CUR_W; col++)
            s_saved[row * CUR_W + col] =
                fb_get_pixel(s_mouse.x + col, s_mouse.y + row);

    s_prev_x = s_mouse.x;
    s_prev_y = s_mouse.y;

    /* Stamp arrow sprite */
    for (row = 0; row < CUR_H; row++) {
        for (col = 0; col < CUR_W; col++) {
            unsigned char k = CURSOR[row][col];
            if (k == 2) continue;
            fb_put_pixel(s_mouse.x + col,
                         s_mouse.y + row,
                         k == 1 ? CURSOR_FG : CURSOR_OUT);
        }
    }

    s_drawn = 1;
}
