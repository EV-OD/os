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

    /* Read one character from keyboard (blocking). */
    buf[0] = (char)keyboard_read_char_blocking();
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

    if (!win) return -1;
    
    /* Fill canvas with colour directly */
    if (win->canvas) {
        int cw = win->w;
        int ch = win->h - TITLE_BAR_H;
        for (int i = 0; i < cw * ch; i++) {
            win->canvas[i] = col;
        }
        win->dirty = 1;
        wm_paint(win);
        fb_flush_rect(win->x, win->y, win->w, win->h);
    }
    return 0;
}

static int sys_gui_text(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    wm_window_t *win = (wm_window_t *)cpu->ebx;
    if (!win) return -1;
    
    int wx = win->x;
    int wy = win->y + TITLE_BAR_H;
    
    int tx = (int)cpu->ecx;
    int ty = (int)cpu->edx;
    const char *text = (const char *)cpu->esi;
    unsigned int col = (unsigned int)cpu->edi;

    /* Draw to back buffer directly over window (quick hack) */
    gfx_draw_text(wx + tx, wy + ty, text, col, COLOR_TRANSPARENT);
    fb_flush_rect(wx + tx, wy + ty, 800, 16); /* conservative flush */
    return 0;
}

static int sys_gui_line(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    wm_window_t *win = (wm_window_t *)cpu->ebx;
    if (!win) return -1;

    int wx = win->x;
    int wy = win->y + TITLE_BAR_H;

    int x0 = (int)cpu->ecx;
    int y0 = (int)cpu->edx;
    int x1 = (int)cpu->esi;
    int y1 = (int)cpu->edi;
    unsigned int col = COLOR_WHITE; /* Hardcoded for now if no color passed */

    gfx_draw_line(wx + x0, wy + y0, wx + x1, wy + y1, col);
    fb_flush_rect(wx, wy, win->w, win->h);
    return 0;
}

static int sys_gui_rect(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    wm_window_t *win = (wm_window_t *)cpu->ebx;
    if (!win) return -1;

    int wx = win->x;
    int wy = win->y + TITLE_BAR_H;

    int r_x = (int)cpu->ecx;
    int r_y = (int)cpu->edx;
    int r_w = (int)cpu->esi;
    int r_h = (int)cpu->edi;
    unsigned int col = COLOR_WHITE; 

    gfx_draw_rect(wx + r_x, wy + r_y, r_w, r_h, col);
    fb_flush_rect(wx + r_x, wy + r_y, r_w, r_h);
    return 0;
}

static int sys_gui_circle(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    wm_window_t *win = (wm_window_t *)cpu->ebx;
    if (!win) return -1;

    int wx = win->x;
    int wy = win->y + TITLE_BAR_H;

    int cx = (int)cpu->ecx;
    int cy = (int)cpu->edx;
    int r  = (int)cpu->esi;
    unsigned int col = (unsigned int)cpu->edi;
    
    gfx_draw_circle(wx + cx, wy + cy, r, col);
    fb_flush_rect(wx + cx - r, wy + cy - r, r*2, r*2);
    return 0;
}

static int sys_gui_flush(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    wm_window_t *win = (wm_window_t *)cpu->ebx;
    if (win) {
        win->dirty = 1;
        wm_paint_all();
        fb_flush();
    }
    return 0;
}

static int sys_gui_poll(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    /* Return latest keyboard character maybe? */
    /* Non-blocking read */
    if (keyboard_available()) {
        return keyboard_read_char();
    }
    return 0;
}

static int sys_gui_wait(struct cpu_state *cpu, struct stack_state *stack)
{
    (void)stack; (void)cpu;
    /* Wait for input */
    return keyboard_read_char_blocking();
}
