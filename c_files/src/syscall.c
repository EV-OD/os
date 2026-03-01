/* =========================================================================
 * syscall.c – System Call Dispatcher
 *
 * When a user-mode program executes  int 0x80, the CPU transitions to
 * ring 0 (via the TSS esp0 / ss0) and enters common_isr_stub which calls
 * interrupt_handler().  For vector 128 the handler delegates here.
 *
 * We read EAX from the saved register state (cpu->eax) to determine the
 * syscall number, extract arguments from EBX/ECX/EDX/ESI/EDI, dispatch
 * to the appropriate handler, and write the return value back into
 * cpu->eax so iret delivers it to the user process.
 * ========================================================================= */

#include "syscall.h"
#include "isr.h"
#include "sched.h"
#include "process.h"
#include "serial.h"
#include "stdio.h"
#include "keyboard.h"
#include "log.h"

/* -------------------------------------------------------------------------
 * Forward declarations of individual syscall handlers
 * ------------------------------------------------------------------------- */
static int sys_exit(struct cpu_state *cpu, struct stack_state *stack);
static int sys_write(struct cpu_state *cpu, struct stack_state *stack);
static int sys_read(struct cpu_state *cpu, struct stack_state *stack);
static int sys_getpid(struct cpu_state *cpu, struct stack_state *stack);
static int sys_yield(struct cpu_state *cpu, struct stack_state *stack);

static int sys_gui_open(struct cpu_state *cpu, struct stack_state *stack);
static int sys_gui_close(struct cpu_state *cpu, struct stack_state *stack);
static int sys_gui_fill(struct cpu_state *cpu, struct stack_state *stack);
static int sys_gui_text(struct cpu_state *cpu, struct stack_state *stack);
static int sys_gui_line(struct cpu_state *cpu, struct stack_state *stack);
static int sys_gui_rect(struct cpu_state *cpu, struct stack_state *stack);
static int sys_gui_circle(struct cpu_state *cpu, struct stack_state *stack);
static int sys_gui_flush(struct cpu_state *cpu, struct stack_state *stack);
static int sys_gui_poll(struct cpu_state *cpu, struct stack_state *stack);
static int sys_gui_wait(struct cpu_state *cpu, struct stack_state *stack);
static int sys_gui_pen(struct cpu_state *cpu, struct stack_state *stack);
static int sys_gui_fill_rect(struct cpu_state *cpu, struct stack_state *stack);
static int sys_gui_fill_circle(struct cpu_state *cpu, struct stack_state *stack);
static int sys_gui_fill_round(struct cpu_state *cpu, struct stack_state *stack);
static int sys_gui_mouse(struct cpu_state *cpu, struct stack_state *stack);

/* -------------------------------------------------------------------------
 * Syscall table
 * ------------------------------------------------------------------------- */
typedef int (*syscall_fn_t)(struct cpu_state *cpu, struct stack_state *stack);

static syscall_fn_t syscall_table[] = {
    [SYS_EXIT]   = sys_exit,
    [SYS_WRITE]  = sys_write,
    [SYS_READ]   = sys_read,
    [SYS_GETPID] = sys_getpid,
    [SYS_YIELD]  = sys_yield,

    [SYS_GUI_OPEN]   = sys_gui_open,
    [SYS_GUI_CLOSE]  = sys_gui_close,
    [SYS_GUI_FILL]   = sys_gui_fill,
    [SYS_GUI_TEXT]   = sys_gui_text,
    [SYS_GUI_LINE]   = sys_gui_line,
    [SYS_GUI_RECT]   = sys_gui_rect,
    [SYS_GUI_CIRCLE] = sys_gui_circle,
    [SYS_GUI_FLUSH]  = sys_gui_flush,
    [SYS_GUI_POLL]   = sys_gui_poll,
    [SYS_GUI_WAIT]   = sys_gui_wait,
    [SYS_GUI_PEN]         = sys_gui_pen,
    [SYS_GUI_FILL_RECT]   = sys_gui_fill_rect,
    [SYS_GUI_FILL_CIRCLE] = sys_gui_fill_circle,
    [SYS_GUI_FILL_ROUND]  = sys_gui_fill_round,
    [SYS_GUI_MOUSE]       = sys_gui_mouse,
};

/* -------------------------------------------------------------------------
 * syscall_dispatch – called from interrupt_handler for vector 128.
 * ------------------------------------------------------------------------- */
void syscall_dispatch(struct cpu_state *cpu, struct stack_state *stack)
{
    unsigned int num = cpu->eax;
    int ret;

    if (num > SYSCALL_MAX || !syscall_table[num]) {
        log_warning("[syscall] invalid syscall number %d from pid=%d eip=0x%x",
                 (int)num,
                 sched_current() ? (int)sched_current()->pid : -1,
                 stack->eip);
        cpu->eax = (unsigned int)(-1);
        return;
    }

    ret = syscall_table[num](cpu, stack);
    cpu->eax = (unsigned int)ret;
}

/* -------------------------------------------------------------------------
 * ISR handler wrapper registered for vector 128
 * ------------------------------------------------------------------------- */
static void syscall_isr(struct cpu_state *cpu, struct stack_state *stack,
                        unsigned int interrupt)
{
    (void)interrupt;
    syscall_dispatch(cpu, stack);
}

/* -------------------------------------------------------------------------
 * syscall_init
 * ------------------------------------------------------------------------- */
void syscall_init(void)
{
    register_interrupt_handler(128, syscall_isr);
    log_info("[syscall] registered int 0x80 handler (%d syscalls)",
             SYSCALL_MAX + 1);
}

/* =========================================================================
 * Individual syscall implementations
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * SYS_EXIT (0) – terminate the current process.
 *
 * EBX = exit status code (stored in process_t for parent to retrieve).
 * Does not return to the calling process.
 * ------------------------------------------------------------------------- */
static int sys_exit(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    process_t *cur = sched_current();
    int status = (int)cpu->ebx;

    if (cur) {
        log_info("[syscall] SYS_EXIT pid=%d status=%d", (int)cur->pid, status);
        cur->exit_status = status;
        cur->state = PROC_DEAD;
        /* The scheduler will reap this process on the next tick.
         * For now, halt until the timer fires and schedules another process. */
        __asm__ volatile("sti");
        for (;;) {
            __asm__ volatile("hlt");
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * SYS_WRITE (1) – write bytes to a file descriptor.
 *
 * EBX = fd (0=stdin, 1=stdout, 2=stderr  – currently stdout/stderr → VGA)
 * ECX = pointer to buffer (user-space address)
 * EDX = number of bytes to write
 *
 * Returns number of bytes written, or -1 on error.
 * TODO: validate user pointer against process page directory.
 * ------------------------------------------------------------------------- */
static int sys_write(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    int fd              = (int)cpu->ebx;
    const char *buf     = (const char *)cpu->ecx;
    int len             = (int)cpu->edx;
    int i;

    if (fd != 1 && fd != 2) {
        return -1;  /* only stdout/stderr supported for now */
    }
    if (len < 0) return -1;

    /* Write each byte to the VGA framebuffer. */
    for (i = 0; i < len; i++) {
        putchar(buf[i]);
    }

    return len;
}

/* -------------------------------------------------------------------------
 * SYS_READ (2) – read bytes from a file descriptor.
 *
 * EBX = fd (0 = stdin → keyboard)
 * ECX = pointer to buffer (user-space address)
 * EDX = max bytes to read
 *
 * Returns number of bytes read, or -1 on error.
 * For stdin, reads one character at a time (blocking via keyboard_getchar).
 * ------------------------------------------------------------------------- */
static int sys_read(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    int fd       = (int)cpu->ebx;
    char *buf    = (char *)cpu->ecx;
    int maxlen   = (int)cpu->edx;

    if (fd != 0) return -1;  /* only stdin for now */
    if (maxlen <= 0) return 0;

    process_t *rdcur = sched_current();

    /* Fast path: data already in the ring buffer. */
    int _c = keyboard_read_char();
    if (_c != -1) {
        buf[0] = (char)_c;
        return 1;
    }

    /* Slow path: put process to sleep until keyboard ISR wakes it. */
    __asm__ volatile("sti");
    for (;;) {
        if (rdcur) {
            if (rdcur->killed) {
                rdcur->exit_status = -1;
                rdcur->state       = PROC_DEAD;
                for (;;) __asm__ volatile("hlt");
            }
            /* Re-arm sleep state before each hlt so sched_tick
             * moves us off the run queue until the keyboard IRQ
             * calls sched_wake_waiters(WAIT_KEY). */
            rdcur->state       = PROC_SLEEPING;
            rdcur->wait_reason = WAIT_KEY;
        }
        __asm__ volatile("hlt");  /* sleep until any IRQ wakes the CPU */
        /* After hlt: either keyboard fired (data in buffer) or timer
         * rescheduled us.  Check for data. */
        _c = keyboard_read_char();
        if (_c != -1) break;
    }
    if (rdcur) {
        rdcur->wait_reason = WAIT_NONE;
    }
    buf[0] = (char)_c;
    return 1;
}

/* -------------------------------------------------------------------------
 * SYS_GETPID (3) – return the current process ID.
 * ------------------------------------------------------------------------- */
static int sys_getpid(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)cpu;
    (void)stack; (void)cpu;
    process_t *cur = sched_current();
    return cur ? (int)cur->pid : -1;
}

/* -------------------------------------------------------------------------
 * SYS_YIELD (4) – voluntarily yield the CPU.
 *
 * Marks the process as RUNNABLE and lets the scheduler pick the next one.
 * For now this is a no-op since the PIT timer drives preemption.
 * ------------------------------------------------------------------------- */
static int sys_yield(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)cpu;
    (void)stack; (void)cpu;
    /* A proper yield would trigger a reschedule; for now just return. */
    return 0;
}

/* =========================================================================
 * GUI System Calls
 * ========================================================================= */
#include "gui/wm.h"
#include "gui/gfx.h"
#include "gui/fb.h"
#include "gui/color.h"
#include "gui/font.h"
#include "gui/canvas.h"
#include "gui/mouse.h"

static int sys_gui_open(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    int x = (int)cpu->ebx;
    int y = (int)cpu->ecx;
    int w = (int)cpu->edx;
    int h = (int)cpu->esi;
    const char *title = (const char *)cpu->edi;
    
    wm_window_t *win = wm_create(x, y, w, h, title ?: "Window");
    if (win) {
        process_t *cur = sched_current();
        if (win) {
            win->no_drag = 0;
        }
        win->owner_pid = cur ? (int)cur->pid : 0;
        wm_alloc_canvas(win);   /* allocate offscreen pixel buffer */
        wm_paint_all();
        fb_flush();
    }
    return (int)win;
}

static int sys_gui_close(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    wm_window_t *win = (wm_window_t *)cpu->ebx;
    if (win) {
        wm_destroy(win);
        wm_invalidate_all();
        wm_paint_all();
        fb_flush();
    }
    return 0;
}

static int sys_gui_fill(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    wm_window_t *win = (wm_window_t *)cpu->ebx;
    unsigned int col = (unsigned int)cpu->ecx;

    if (!win || !win->canvas) return -1;
    win->bg_color = col;
    cnv_clear(win->canvas, win->w, win->h - TITLE_BAR_H, col);
    return 0;
}

static int sys_gui_text(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    wm_window_t *win = (wm_window_t *)cpu->ebx;
    if (!win || !win->canvas) return -1;

    int tx             = (int)cpu->ecx;
    int ty             = (int)cpu->edx;
    const char *text   = (const char *)cpu->esi;
    unsigned int col   = (unsigned int)cpu->edi;

    cnv_draw_str(win->canvas, win->w, win->h - TITLE_BAR_H,
                 tx, ty, text, col, COLOR_TRANSPARENT);
    return 0;
}

static int sys_gui_line(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    wm_window_t *win = (wm_window_t *)cpu->ebx;
    if (!win || !win->canvas) return -1;

    int x0 = (int)cpu->ecx;
    int y0 = (int)cpu->edx;
    int x1 = (int)cpu->esi;
    int y1 = (int)cpu->edi;

    cnv_draw_line(win->canvas, win->w, win->h - TITLE_BAR_H,
                  x0, y0, x1, y1, win->pen_color);
    return 0;
}

static int sys_gui_rect(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    wm_window_t *win = (wm_window_t *)cpu->ebx;
    if (!win || !win->canvas) return -1;

    int rx = (int)cpu->ecx;
    int ry = (int)cpu->edx;
    int rw = (int)cpu->esi;
    int rh = (int)cpu->edi;

    cnv_draw_rect(win->canvas, win->w, win->h - TITLE_BAR_H,
                  rx, ry, rw, rh, win->pen_color);
    return 0;
}

static int sys_gui_circle(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    wm_window_t *win = (wm_window_t *)cpu->ebx;
    if (!win || !win->canvas) return -1;

    int cx = (int)cpu->ecx;
    int cy = (int)cpu->edx;
    int r  = (int)cpu->esi;
    unsigned int col = (unsigned int)cpu->edi;

    cnv_draw_circle(win->canvas, win->w, win->h - TITLE_BAR_H,
                    cx, cy, r, col);
    return 0;
}

static int sys_gui_flush(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    wm_window_t *win = (wm_window_t *)cpu->ebx;
    if (win) {
        /*
         * Double-buffer present: copy draw canvas → front canvas.
         * The compositor (desktop_run) always blits front_canvas, which
         * only contains complete frames.  Without this the compositor
         * catches the canvas mid-clear (all-black) causing flicker even
         * when the window is standing still.
         */
        wm_present_canvas(win);
        /* Update back-buffer for this window (front_canvas blit + title bar).
         * Do NOT call fb_flush_rect – the compositor owns all fb_flush calls
         * at FRAME_TICKS cadence (~50 fps). */
        wm_paint(win);
    }
    return 0;
}

static int sys_gui_poll(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    /* If the window was closed externally, return EV_CLOSE (-1) so
     * poll-based event loops can exit gracefully. */
    process_t *cur = sched_current();
    if (cur && cur->killed) return -1;
    if (keyboard_available()) {
        return keyboard_read_char();
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * SYS_GUI_PEN (16) – set the current stroke/pen colour for a window.
 * EBX = win, ECX = color (0x00RRGGBB)
 * All subsequent line, rect, circle calls use this colour.
 * ------------------------------------------------------------------------- */
static int sys_gui_pen(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    wm_window_t *win = (wm_window_t *)cpu->ebx;
    if (!win) return -1;
    win->pen_color = (unsigned int)cpu->ecx;
    return 0;
}

/* -------------------------------------------------------------------------
 * SYS_GUI_FILL_RECT (17) – filled rectangle on canvas.
 * EBX=win, ECX=x, EDX=y, ESI=w, EDI=h  (uses pen_color as fill)
 * ------------------------------------------------------------------------- */
static int sys_gui_fill_rect(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    wm_window_t *win = (wm_window_t *)cpu->ebx;
    if (!win || !win->canvas) return -1;
    int rx = (int)cpu->ecx, ry = (int)cpu->edx;
    int rw = (int)cpu->esi, rh = (int)cpu->edi;
    cnv_fill_rect(win->canvas, win->w, win->h - TITLE_BAR_H,
                  rx, ry, rw, rh, win->pen_color);
    return 0;
}

/* -------------------------------------------------------------------------
 * SYS_GUI_FILL_CIRCLE (18) – filled circle on canvas.
 * EBX=win, ECX=cx, EDX=cy, ESI=r, EDI=color
 * ------------------------------------------------------------------------- */
static int sys_gui_fill_circle(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    wm_window_t *win = (wm_window_t *)cpu->ebx;
    if (!win || !win->canvas) return -1;
    int cx = (int)cpu->ecx, cy = (int)cpu->edx;
    int r  = (int)cpu->esi;
    unsigned int col = (unsigned int)cpu->edi;
    cnv_fill_circle(win->canvas, win->w, win->h - TITLE_BAR_H,
                    cx, cy, r, col);
    return 0;
}

/* -------------------------------------------------------------------------
 * SYS_GUI_FILL_ROUND (19) – filled rounded rectangle.
 * EBX=win, ECX=x, EDX=y, ESI=packed(w<<16|h), EDI=packed(radius<<16|color_hi)
 * — packing needed because we only have 5 register args total.
 * Actually: EBX=win, ECX=x|(y<<16), EDX=w|(h<<16), ESI=radius, EDI=color
 * (x and y fit in 16 bits for any reasonable screen size)
 * ------------------------------------------------------------------------- */
static int sys_gui_fill_round(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    wm_window_t *win = (wm_window_t *)cpu->ebx;
    if (!win || !win->canvas) return -1;
    /* Unpack: ECX = x | (y << 16),  EDX = w | (h << 16) */
    int x = (int)(cpu->ecx & 0xFFFF);
    int y = (int)((cpu->ecx >> 16) & 0xFFFF);
    int w = (int)(cpu->edx & 0xFFFF);
    int h = (int)((cpu->edx >> 16) & 0xFFFF);
    int radius = (int)cpu->esi;
    unsigned int col = (unsigned int)cpu->edi;
    cnv_fill_round_rect(win->canvas, win->w, win->h - TITLE_BAR_H,
                        x, y, w, h, radius, col);
    return 0;
}

/* -------------------------------------------------------------------------
 * SYS_GUI_MOUSE (20) – packed mouse state relative to window client area.
 * EBX = win
 * Returns: (x & 0xFFF) | ((y & 0xFFF) << 12) | ((buttons & 0xFF) << 24)
 * x,y are client-area coordinates (relative to canvas top-left).
 * buttons: bit0 = left, bit1 = right.
 * ------------------------------------------------------------------------- */
static int sys_gui_mouse(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack;
    wm_window_t *win = (wm_window_t *)cpu->ebx;
    mouse_state_t ms = mouse_get();
    int rx, ry;
    if (win) {
        rx = ms.x - win->x;
        ry = ms.y - (win->y + TITLE_BAR_H);
        /* clamp to client area */
        if (rx < 0)   rx = 0;
        if (ry < 0)   ry = 0;
        if (rx >= win->w)                rx = win->w - 1;
        if (ry >= win->h - TITLE_BAR_H) ry = win->h - TITLE_BAR_H - 1;
    } else {
        rx = ms.x;
        ry = ms.y;
    }
    /* x in bits 0-11, y in bits 12-23, buttons in bits 24-25 */
    return (rx & 0xFFF) | ((ry & 0xFFF) << 12) | (((int)ms.buttons & 0xFF) << 24);
}

static int sys_gui_wait(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    /* Block until a key is pressed.
     * sti lets keyboard/timer/mouse IRQs fire; hlt sleeps until one does.
     * iret later restores EFLAGS from the saved user frame (IF=1). */
    process_t *wcur = sched_current();

    /* Fast path: key already waiting. */
    int c = keyboard_read_char();
    if (c != -1) return c;

    /* Slow path: block until a key arrives or the window is closed. */
    __asm__ volatile("sti");
    for (;;) {
        if (wcur) {
            if (wcur->killed) {
                /* Window was closed (or Ctrl+C).  Returning -1 while
                 * still runnable causes an infinite spin (the ROX program
                 * re-enters gui_wait every instruction, sti never fires
                 * long enough for the timer → system-wide freeze).
                 * Terminate the process here instead; the shell's
                 * process_wait() will see PROC_DEAD and unblock. */
                wcur->wait_reason = WAIT_NONE;
                wcur->exit_status = -1;
                wcur->state       = PROC_DEAD;
                for (;;) __asm__ volatile("hlt");
            }
            /* Re-arm sleep state before each hlt. */
            wcur->state       = PROC_SLEEPING;
            wcur->wait_reason = WAIT_KEY;
        }
        __asm__ volatile("hlt");
        c = keyboard_read_char();
        if (c != -1) break;
    }
    if (wcur) {
        wcur->wait_reason = WAIT_NONE;
    }
    return c;
}
