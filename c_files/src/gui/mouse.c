/* =========================================================================
 * gui/mouse.c – PS/2 mouse driver (IRQ12, 3-byte packet protocol)
 *
 * Initialization sequence:
 *   1. Disable keyboard to stabilise the 8042 controller.
 *   2. Enable the PS/2 aux port.
 *   3. Set Compaq status byte bit 1 (enable mouse IRQ12).
 *   4. Send Reset, Set-Defaults, Enable-Reporting to the mouse.
 *   5. Re-enable keyboard.
 *   6. Register IRQ12 handler (vector 44), unmask IRQ12.
 *
 * The IRQ12 handler accumulates 3-byte packets and converts delta
 * movements to absolute screen coordinates.
 * ========================================================================= */

#include "gui/mouse.h"
#include "gui/fb.h"
#include "gui/gfx.h"
#include "gui/color.h"
#include "isr.h"
#include "pic.h"
#include "stdio.h"     /* outb / inb */
#include "log.h"

/* -------------------------------------------------------------------------
 * 8042 / PS/2 port constants
 * ------------------------------------------------------------------------- */

#define PS2_DATA_PORT   0x60
#define PS2_CMD_PORT    0x64   /* write = command, read = status */
#define PS2_STATUS_OBF  0x01   /* output buffer full (data ready to read)  */
#define PS2_STATUS_IBF  0x02   /* input buffer full  (busy, don't write)   */

/* Commands sent to the controller (port 0x64) */
#define PS2_CMD_READ_COMPAQ  0x20  /* read  Compaq status byte */
#define PS2_CMD_WRITE_COMPAQ 0x60  /* write Compaq status byte */
#define PS2_CMD_AUX_DISABLE  0xA7  /* disable PS/2 mouse port  */
#define PS2_CMD_AUX_ENABLE   0xA8  /* enable  PS/2 mouse port  */
#define PS2_CMD_WRITE_AUX    0xD4  /* next byte → mouse         */
#define PS2_CMD_KBD_DISABLE  0xAD
#define PS2_CMD_KBD_ENABLE   0xAE

/* Commands sent to the mouse (via 0xD4 + data port) */
#define MOUSE_CMD_RESET      0xFF
#define MOUSE_CMD_DEFAULTS   0xF6
#define MOUSE_CMD_DATA_ON    0xF4

/* IRQ12 = PIC2 offset (0x28) + 4 = vector 44 */
#define MOUSE_IRQ_VECTOR     44
#define MOUSE_IRQ_LINE       12

/* -------------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------------- */

static mouse_state_t s_mouse;
static int           s_mouse_ready = 0;

/* Interrupt packet accumulation */
static unsigned char s_packet[3];
static int           s_packet_byte = 0;

/* Cursor save-under (pixels behind cursor sprite) */
#define CURSOR_W  12
#define CURSOR_H  19

static unsigned int  s_cursor_saved[CURSOR_W * CURSOR_H];
static int           s_cursor_drawn  = 0;
static int           s_cursor_save_x = 0;
static int           s_cursor_save_y = 0;

/* Cursor sprite: 1=fg, 0=bg, 2=transparent */
static const unsigned char s_cursor_sprite[CURSOR_H][CURSOR_W] = {
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

/* -------------------------------------------------------------------------
 * Low-level 8042 helpers
 * ------------------------------------------------------------------------- */

static void ps2_wait_write(void)
{
    unsigned int timeout = 100000;
    while (timeout-- && (inb(PS2_CMD_PORT) & PS2_STATUS_IBF)) {}
}

static void ps2_wait_read(void)
{
    unsigned int timeout = 100000;
    while (timeout-- && !(inb(PS2_CMD_PORT) & PS2_STATUS_OBF)) {}
}

static void mouse_write(unsigned char byte)
{
    ps2_wait_write();
    outb(PS2_CMD_PORT, PS2_CMD_WRITE_AUX);
    ps2_wait_write();
    outb(PS2_DATA_PORT, byte);
}

static unsigned char mouse_read(void)
{
    ps2_wait_read();
    return inb(PS2_DATA_PORT);
}

/* -------------------------------------------------------------------------
 * Packet processing
 * ------------------------------------------------------------------------- */

static int s_clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void mouse_process_packet(unsigned char *p)
{
    int dx, dy;

    /* Bit 3 should always be 1; if not, packet sync is lost. */
    if (!(p[0] & 0x08)) {
        s_packet_byte = 0;
        return;
    }

    /* Discard overflow packets (bits 6 & 7 of byte 0). */
    if (p[0] & 0xC0) return;

    /* 9-bit signed X delta: sign bit = p[0] bit 4, data = p[1].
     * If sign bit is set, the 9-bit value is negative: p[1] - 256.     */
    dx = (int)p[1];
    if (p[0] & 0x10) dx -= 256;

    /* 9-bit signed Y delta: sign bit = p[0] bit 5, data = p[2].
     * PS/2 Y axis is positive-up; screen Y is positive-down → negate. */
    dy = (int)p[2];
    if (p[0] & 0x20) dy -= 256;
    dy = -dy;

    /* Clamp to screen */
    s_mouse.x       = s_clamp(s_mouse.x + dx, 0, (int)fb_width()  - 1);
    s_mouse.y       = s_clamp(s_mouse.y + dy, 0, (int)fb_height() - 1);
    s_mouse.buttons = p[0] & 0x07;
}

/* -------------------------------------------------------------------------
 * IRQ12 handler
 * ------------------------------------------------------------------------- */

static void mouse_irq_handler(struct cpu_state *cpu,
                               struct stack_state *stack,
                               unsigned int interrupt)
{
    (void)cpu; (void)stack; (void)interrupt;

    s_packet[s_packet_byte++] = inb(PS2_DATA_PORT);
    if (s_packet_byte == 3) {
        s_packet_byte = 0;
        mouse_process_packet(s_packet);
    }
}

/* -------------------------------------------------------------------------
 * mouse_init
 * ------------------------------------------------------------------------- */

void mouse_init(void)
{
    unsigned char status;

    /* 1. Register IRQ12 handler and unmask BEFORE enabling aux IRQ to
     *    prevent "Unhandled interrupt: 44" if the mouse fires early. */
    register_interrupt_handler(MOUSE_IRQ_VECTOR, mouse_irq_handler);
    pic_clear_mask(MOUSE_IRQ_LINE);

    /* 2. Disable PS/2 keyboard temporarily */
    ps2_wait_write();
    outb(PS2_CMD_PORT, PS2_CMD_KBD_DISABLE);

    /* 3. Enable PS/2 auxiliary (mouse) port */
    ps2_wait_write();
    outb(PS2_CMD_PORT, PS2_CMD_AUX_ENABLE);

    /* 4. Enable mouse interrupts via Compaq Status Byte bit 1 */
    ps2_wait_write();
    outb(PS2_CMD_PORT, PS2_CMD_READ_COMPAQ);
    ps2_wait_read();
    status = inb(PS2_DATA_PORT);
    status |= 0x02;   /* enable aux IRQ */
    status &= ~0x20;  /* clear "mouse disabled" bit */
    ps2_wait_write();
    outb(PS2_CMD_PORT, PS2_CMD_WRITE_COMPAQ);
    ps2_wait_write();
    outb(PS2_DATA_PORT, status);

    /* 5. Reset mouse; read 0xAA (self-test pass) + 0x00 (device ID) */
    mouse_write(MOUSE_CMD_RESET);
    mouse_read(); /* 0xFA ack */
    mouse_read(); /* 0xAA self-test */
    mouse_read(); /* 0x00 device ID */

    /* 6. Set defaults */
    mouse_write(MOUSE_CMD_DEFAULTS);
    mouse_read(); /* 0xFA ack */

    /* 7. Enable data reporting */
    mouse_write(MOUSE_CMD_DATA_ON);
    mouse_read(); /* 0xFA ack */

    /* 8. Re-enable keyboard */
    ps2_wait_write();
    outb(PS2_CMD_PORT, PS2_CMD_KBD_ENABLE);

    /* Centre cursor on screen */
    s_mouse.x = (int)(fb_width()  / 2);
    s_mouse.y = (int)(fb_height() / 2);
    s_mouse.buttons = 0;

    s_mouse_ready = 1;
    log_info("[mouse] PS/2 mouse initialised (IRQ12)");
}

/* -------------------------------------------------------------------------
 * mouse_get
 * ------------------------------------------------------------------------- */

mouse_state_t mouse_get(void)
{
    return s_mouse;
}

int mouse_available(void)
{
    return s_mouse_ready;
}

/* -------------------------------------------------------------------------
 * mouse_draw_cursor
 *
 * 1. Restore the pixels saved from the previous frame.
 * 2. Save the pixels behind the new cursor position.
 * 3. Draw the arrow sprite.
 * ------------------------------------------------------------------------- */

void mouse_draw_cursor(void)
{
    int mx, my, row, col;
    unsigned char kind;
    color_t px;

    if (!s_mouse_ready || !fb_available()) return;

    mx = s_mouse.x;
    my = s_mouse.y;

    /* Restore previous under-cursor pixels */
    if (s_cursor_drawn) {
        for (row = 0; row < CURSOR_H; row++)
            for (col = 0; col < CURSOR_W; col++)
                fb_put_pixel(s_cursor_save_x + col,
                             s_cursor_save_y + row,
                             s_cursor_saved[row * CURSOR_W + col]);
    }

    /* Save pixels behind new cursor position */
    for (row = 0; row < CURSOR_H; row++)
        for (col = 0; col < CURSOR_W; col++)
            s_cursor_saved[row * CURSOR_W + col] =
                fb_get_pixel(mx + col, my + row);

    s_cursor_save_x = mx;
    s_cursor_save_y = my;

    /* Draw sprite */
    for (row = 0; row < CURSOR_H; row++) {
        for (col = 0; col < CURSOR_W; col++) {
            kind = s_cursor_sprite[row][col];
            if (kind == 2) continue;   /* transparent */
            px = (kind == 1) ? COLOR_CURSOR_FG : COLOR_CURSOR_BORDER;
            fb_put_pixel(mx + col, my + row, px);
        }
    }

    s_cursor_drawn = 1;
}
