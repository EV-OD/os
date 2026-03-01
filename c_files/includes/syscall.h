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

/* Text buffer syscalls (for rxt editor) */
#define SYS_TBUF_OPEN      21  /* int  tbuf_open(const char *path)               */
#define SYS_TBUF_CLOSE     22  /* int  tbuf_close(int h)                         */
#define SYS_TBUF_SAVE      23  /* int  tbuf_save(int h)                          */
#define SYS_TBUF_GETLINE   24  /* const char *tbuf_getline(int h, int n)         */
#define SYS_TBUF_INPUT     25  /* int  tbuf_input(int h, int key)                */
#define SYS_TBUF_LINECOUNT 26  /* int  tbuf_linecount(int h)                     */
#define SYS_TBUF_CURSOR    27  /* int  tbuf_cursor(int h)  (line | col<<16)      */
#define SYS_TBUF_NUMSTR    28  /* const char *tbuf_numstr(int h, int n)          */
#define SYS_GETARG         29  /* const char *getarg(int idx)                    */
#define SYS_SPAWN_TERM     30  /* void  spawn_term(void)   – open a new terminal  */
#define SYS_TBUF_SAVEAS    31  /* int   tbuf_saveas(int h, void *win)             */

#undef SYSCALL_MAX
#define SYSCALL_MAX  31


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
