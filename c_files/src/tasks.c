/* =========================================================================
 * tasks.c – Kernel-mode test tasks for CFS scheduler demonstration
 *
 * Each task runs in an infinite loop, incrementing a counter and writing
 * it to a dedicated row on the VGA framebuffer.  The different nice values
 * mean the CFS scheduler allocates proportionally different CPU shares,
 * which is visible as different counter speeds on screen.
 *
 * VGA layout (80 columns × 25 rows):
 *   Row  8  : "TASK  niceval  counter" header
 *   Row  9  : task_a  (nice=-5,  weight=3121 → gets ~3× baseline CPU)
 *   Row 10  : task_b  (nice= 0,  weight=1024 → baseline)
 *   Row 11  : task_c  (nice=+5,  weight= 335 → gets ~0.3× baseline CPU)
 *   Row 12  : task_idle (nice=+19, weight=15 → minimal CPU)
 *
 * The counter values after running for a few seconds should reflect the
 * approximate 87:1 ratio between the extreme weights (nice -20 vs nice +19).
 * ========================================================================= */

#include "tasks.h"
#include "stdio.h"
#include "string.h"

/* -------------------------------------------------------------------------
 * VGA framebuffer virtual address (phys 0xB8000, mapped in first 4 MB).
 * Each cell = 2 bytes: [character byte][attribute byte].
 * Attribute = (bg << 4) | fg.  0x0F = white on black.
 * ------------------------------------------------------------------------- */
#define VGA_BASE  ((volatile unsigned short *)0xC00B8000u)
#define VGA_COLS  80

/* -------------------------------------------------------------------------
 * Colour attributes for each task.
 * (background << 4) | foreground, e.g. 0x0A = bright green on black.
 * ------------------------------------------------------------------------- */
#define ATTR_HEADER   0x0Fu  /* white on black          */
#define ATTR_TASK_A   0x0Au  /* bright green on black   */
#define ATTR_TASK_B   0x0Bu  /* bright cyan  on black   */
#define ATTR_TASK_C   0x0Du  /* bright magenta on black */
#define ATTR_IDLE     0x08u  /* dark grey on black      */

/* =========================================================================
 * vga_puts_at – write a NUL-terminated string at VGA row/col with an attr.
 * Clears to end-of-field (width characters) to overwrite stale digits.
 * ========================================================================= */
static void vga_puts_at(unsigned int row, unsigned int col,
                         unsigned char attr, const char *s, unsigned int width)
{
    volatile unsigned short *cell = VGA_BASE + row * VGA_COLS + col;
    unsigned int written = 0;

    while (*s && written < width) {
        *cell++ = (unsigned short)((attr << 8) | (unsigned char)*s);
        s++;
        written++;
    }
    /* Pad with spaces to clear leftover characters. */
    while (written < width) {
        *cell++ = (unsigned short)((attr << 8) | ' ');
        written++;
    }
}

/* =========================================================================
 * uint_to_str – convert an unsigned integer to a decimal string.
 * Written into buf (caller-supplied, must be ≥ 11 bytes for 32-bit max).
 * Returns pointer to buf.
 * ========================================================================= */
static char *uint_to_str(unsigned int n, char *buf, unsigned int buflen)
{
    char tmp[12];
    unsigned int i = 0;
    unsigned int j = 0;

    if (n == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return buf;
    }

    while (n > 0 && i < sizeof(tmp) - 1) {
        tmp[i++] = (char)('0' + (n % 10));
        n /= 10;
    }

    /* Reverse digits into buf. */
    while (i > 0 && j < buflen - 1) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
    return buf;
}

/* =========================================================================
 * draw_task_row – update a task's display row on the VGA screen.
 *
 *  row    – VGA row number (0-24)
 *  label  – short task label string, e.g. "TASK A"
 *  nice   – nice value shown in the middle column
 *  count  – current loop counter
 *  attr   – VGA colour attribute
 * ========================================================================= */
static void draw_task_row(unsigned int row, const char *label, int nice,
                           unsigned int count, unsigned char attr)
{
    char nicebuf[16];
    char cntbuf[16];

    /* Left column: task name (10 chars) */
    vga_puts_at(row, 0, attr, label, 10);

    /* Middle column: nice value (8 chars, right-padded) */
    if (nice < 0) {
        nicebuf[0] = '-';
        uint_to_str((unsigned int)(-nice), nicebuf + 1, sizeof(nicebuf) - 1);
    } else {
        nicebuf[0] = '+';
        uint_to_str((unsigned int)nice, nicebuf + 1, sizeof(nicebuf) - 1);
    }
    vga_puts_at(row, 12, attr, nicebuf, 8);

    /* Right column: counter value (12 chars) */
    uint_to_str(count, cntbuf, sizeof(cntbuf));
    vga_puts_at(row, 22, attr, cntbuf, 14);
}

/* =========================================================================
 * draw_header – write the column headers once (called by task_a on first tick).
 * ========================================================================= */
static void draw_header(void)
{
    vga_puts_at(8, 0, ATTR_HEADER, "TASK NAME", 10);
    vga_puts_at(8, 12, ATTR_HEADER, "NICE", 8);
    vga_puts_at(8, 22, ATTR_HEADER, "LOOP COUNT (CFS demo)", 30);
}

/* =========================================================================
 * Task bodies
 * ========================================================================= */

/* --- task_a: nice=-5 (high priority, ~3× baseline CPU share) ------------ */
void task_a(void)
{
    unsigned int count = 0;

    draw_header();  /* task_a draws the header once at startup */

    while (1) {
        draw_task_row(9, "task_a", -5, count, ATTR_TASK_A);
        count++;
    }
}

/* --- task_b: nice=0 (normal / baseline priority) ------------------------ */
void task_b(void)
{
    unsigned int count = 0;

    while (1) {
        draw_task_row(10, "task_b", 0, count, ATTR_TASK_B);
        count++;
    }
}

/* --- task_c: nice=+5 (low priority, ~0.3× baseline CPU share) ----------- */
void task_c(void)
{
    unsigned int count = 0;

    while (1) {
        draw_task_row(11, "task_c", 5, count, ATTR_TASK_C);
        count++;
    }
}

/* --- task_idle: nice=+19 (background / near-idle, minimal CPU) ---------- */
void task_idle(void)
{
    unsigned int count = 0;

    while (1) {
        draw_task_row(12, "task_idle", 19, count, ATTR_IDLE);
        count++;

        /*
         * The idle task voluntarily halt the CPU if there is nothing else
         * to do.  In a real OS this would be `hlt` inside an interrupt-
         * enabled loop.  Here we just spin since we always have other tasks.
         */
    }
}
