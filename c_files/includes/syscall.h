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
#define SYS_GUI_FILL   8   /* clear canvas with colour         */
#define SYS_GUI_TEXT   9   /* draw text onto canvas             */
#define SYS_GUI_LINE   10  /* stroke line (uses pen_color)     */
#define SYS_GUI_RECT   11  /* stroke rect (uses pen_color)     */
#define SYS_GUI_CIRCLE 12  /* stroke circle                     */
#define SYS_GUI_FLUSH  13  /* blit canvas → screen              */
#define SYS_GUI_POLL   14  /* non-blocking key check            */
#define SYS_GUI_WAIT   15  /* blocking key wait                 */
#define SYS_GUI_PEN    16  /* set pen/stroke colour             */
#define SYS_GUI_FILL_RECT   17  /* filled rectangle             */
#define SYS_GUI_FILL_CIRCLE 18  /* filled circle                */
#define SYS_GUI_FILL_ROUND  19  /* filled rounded rectangle     */
#define SYS_GUI_MOUSE       20  /* packed mouse state (x|y<<12|btn<<24) for window */

#undef SYSCALL_MAX
#define SYSCALL_MAX  20


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
