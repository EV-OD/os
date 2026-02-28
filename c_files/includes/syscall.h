#ifndef SYSCALL_H
#define SYSCALL_H

/* =========================================================================
 * syscall.h – System Call Interface
 *
 * User-mode programs invoke system calls via  int 0x80  (vector 128).
 * The syscall number is passed in EAX; arguments in EBX, ECX, EDX, ESI, EDI.
 * The return value is placed in EAX by the kernel before returning to user.
 *
 * Convention  (matches Linux i386 syscall ABI for familiarity):
 *   EAX = syscall number
 *   EBX = arg1
 *   ECX = arg2
 *   EDX = arg3
 *   ESI = arg4
 *   EDI = arg5
 *   Return value → EAX
 * ========================================================================= */

#include "idt.h"

/* -------------------------------------------------------------------------
 * Syscall numbers
 * ------------------------------------------------------------------------- */
#define SYS_EXIT     0   /* void _exit(int status)                          */
#define SYS_WRITE    1   /* int  write(int fd, const char *buf, int len)    */
#define SYS_READ     2   /* int  read(int fd, char *buf, int len)           */
#define SYS_GETPID   3   /* int  getpid(void)                               */
#define SYS_YIELD    4   /* void yield(void)                                */
#define SYS_SBRK     5   /* void *sbrk(int increment)  (future)            */
#define SYS_GUI_OPEN   6
#define SYS_GUI_CLOSE  7
#define SYS_GUI_FILL   8
#define SYS_GUI_TEXT   9
#define SYS_GUI_LINE   10
#define SYS_GUI_RECT   11
#define SYS_GUI_CIRCLE 12
#define SYS_GUI_FLUSH  13
#define SYS_GUI_POLL   14
#define SYS_GUI_WAIT   15

#undef SYSCALL_MAX
#define SYSCALL_MAX  15


/* -------------------------------------------------------------------------
 * Kernel-side dispatcher – called from interrupt_handler when int == 128.
 *
 * Reads EAX from the saved cpu_state to determine the syscall number,
 * then dispatches to the appropriate handler.  Writes the return value
 * back into cpu->eax so iret places it in the user's EAX.
 * ------------------------------------------------------------------------- */
void syscall_dispatch(struct cpu_state *cpu, struct stack_state *stack);

/* -------------------------------------------------------------------------
 * Initialise the syscall subsystem (register the int 0x80 handler).
 * Must be called after isr_install().
 * ------------------------------------------------------------------------- */
void syscall_init(void);

#endif /* SYSCALL_H */
