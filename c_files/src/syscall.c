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
    (void)stack;
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
    (void)stack;
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
    (void)stack;
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
    (void)stack;
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
    (void)stack;
    /* A proper yield would trigger a reschedule; for now just return. */
    return 0;
}
