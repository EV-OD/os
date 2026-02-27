/* =========================================================================
 * process.c – Process creation and kernel-stack frame setup
 *
 * Creates kernel-mode processes whose context can be saved and restored
 * entirely through the normal interrupt-return mechanism (popa + iret).
 *
 * Kernel stack layout for a new process (grows DOWNWARD from kstack_top):
 *
 *   kstack + PROC_KSTACK_SIZE - 4   ← EFLAGS (IF=1, bit 9)
 *   kstack + PROC_KSTACK_SIZE - 8   ← CS = 0x08 (kernel code)
 *   kstack + PROC_KSTACK_SIZE - 12  ← EIP = entry_point
 *   kstack + PROC_KSTACK_SIZE - 16  ← dummy error_code = 0
 *   kstack + PROC_KSTACK_SIZE - 20  ← dummy int_number = 32 (IRQ0)
 *   kstack + PROC_KSTACK_SIZE - 52  ← pusha block (32 B, all zeros)
 *                                   ← saved_esp (= process_t.saved_esp)
 *
 * On restore: set esp = saved_esp, popa, add esp,8, iret.
 * iret pops EIP/CS/EFLAGS (no ring change since both are ring 0).
 * ========================================================================= */

#include "process.h"
#include "kheap.h"
#include "string.h"
#include "log.h"

/* -------------------------------------------------------------------------
 * Module-private state
 * ------------------------------------------------------------------------- */
static unsigned int next_pid = 1;

/* -------------------------------------------------------------------------
 * process_init
 * ------------------------------------------------------------------------- */
void process_init(void)
{
    next_pid = 1;
    log_info("[process] process subsystem ready");
}

/* -------------------------------------------------------------------------
 * process_get_next_pid
 * ------------------------------------------------------------------------- */
unsigned int process_get_next_pid(void)
{
    return next_pid++;
}

/* -------------------------------------------------------------------------
 * process_create
 *
 * Builds the initial kernel stack frame for a new kernel-mode process.
 * The frame mirrors what common_isr_stub would have produced had the
 * process been interrupted by IRQ0 while executing its first instruction.
 * ------------------------------------------------------------------------- */
process_t *process_create(const char *name, void (*entry)(void), int nice)
{
    process_t    *proc;
    unsigned int  kstack_top;
    unsigned int *sp;

    /* Allocate the process descriptor. */
    proc = (process_t *)kmalloc(sizeof(process_t));
    if (!proc) {
        log_error("[process] failed to allocate process descriptor for '%s'", name);
        return (process_t *)0;
    }

    /* Allocate the kernel stack. */
    proc->kstack = (unsigned char *)kmalloc(PROC_KSTACK_SIZE);
    if (!proc->kstack) {
        kfree(proc);
        log_error("[process] failed to allocate kernel stack for '%s'", name);
        return (process_t *)0;
    }

    /* Zero the kernel stack to avoid stale data in saved registers. */
    memset(proc->kstack, 0, PROC_KSTACK_SIZE);

    /* Fill in the descriptor. */
    proc->pid   = next_pid++;
    proc->name  = name;
    proc->nice  = nice;
    proc->weight = nice_to_weight_val(nice);
    proc->vruntime = 0;   /* scheduler sets this to min_vruntime before enqueue */
    proc->state = PROC_RUNNABLE;
    proc->next  = (process_t *)0;

    /*
     * Build the initial stack frame.
     * kstack_top = one-past-end of the kernel stack (= kstack + size).
     * We write 32-bit words downward from kstack_top.
     *
     * Frame layout (addresses decreasing):
     *   [kstack_top -  4]  EFLAGS = 0x0202  (IF=1, Reserved bit 1 = 1)
     *   [kstack_top -  8]  CS     = 0x0008  (kernel code, ring 0)
     *   [kstack_top - 12]  EIP    = entry
     *   [kstack_top - 16]  error_code = 0   (dummy; IRQ macro pushes 0)
     *   [kstack_top - 20]  int_number = 32  (dummy; IRQ0 vector)
     *   [kstack_top - 52]  pusha block      (8 × 4 = 32 bytes, zeroed above)
     *
     *   saved_esp = kstack_top - 52  (= &most-recently-pushed EDI)
     */
    kstack_top = (unsigned int)(proc->kstack + PROC_KSTACK_SIZE);

    /* Use a uint32_t pointer to write the initial frame. */
    sp = (unsigned int *)(kstack_top - 4);  /* sp[0] is at kstack_top - 4 */

    sp[0]  = 0x00000202u;              /* EFLAGS: IF=1 */
    sp[-1] = 0x00000008u;              /* CS: kernel code selector */
    sp[-2] = (unsigned int)entry;      /* EIP: process entry point */
    sp[-3] = 0u;                       /* dummy error code */
    sp[-4] = 32u;                      /* dummy interrupt number (IRQ0) */
    /* sp[-5] through sp[-12]: the 8 pusha registers, already zero */

    /*
     * saved_esp points to the top of the pusha block (where EDI lives).
     *  kstack_top - 4  = EFLAGS  slot     = sp[0]
     *  kstack_top - 8  = CS      slot     = sp[-1]
     *  kstack_top - 12 = EIP     slot     = sp[-2]
     *  kstack_top - 16 = error   slot     = sp[-3]
     *  kstack_top - 20 = int_num slot     = sp[-4]
     *  kstack_top - 24 = EAX     (pusha)  = sp[-5]  (first pushed by pusha)
     *  kstack_top - 28 = ECX     (pusha)  = sp[-6]
     *  kstack_top - 32 = EDX     (pusha)  = sp[-7]
     *  kstack_top - 36 = EBX     (pusha)  = sp[-8]
     *  kstack_top - 40 = ESP_sav (pusha)  = sp[-9]
     *  kstack_top - 44 = EBP     (pusha)  = sp[-10]
     *  kstack_top - 48 = ESI     (pusha)  = sp[-11]
     *  kstack_top - 52 = EDI     (pusha, last pushed) = sp[-12]
     *                   ← saved_esp points here
     */
    proc->saved_esp = kstack_top - 52u;

    log_info("[process] created '%s' pid=%d nice=%d weight=%d entry=0x%x kstack=0x%x",
             name, (int)proc->pid, nice, (int)proc->weight,
             (unsigned int)entry, (unsigned int)proc->kstack);

    return proc;
}
