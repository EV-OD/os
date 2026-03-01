/* =========================================================================
 * process.c – Process creation and kernel-stack frame setup
 *
 * Creates kernel-mode and user-mode processes whose context can be saved
 * and restored through the normal interrupt-return mechanism:
 *   pop seg_regs + popa + add esp,8 + iret
 *
 * Kernel-mode (ring 0) stack layout for a new process:
 *
 *   kstack + KSTACK_SIZE -  4   ← EFLAGS (IF=1, bit 9)
 *   kstack + KSTACK_SIZE -  8   ← CS = 0x08 (kernel code)
 *   kstack + KSTACK_SIZE - 12   ← EIP = entry_point
 *   kstack + KSTACK_SIZE - 16   ← error_code = 0  (dummy)
 *   kstack + KSTACK_SIZE - 20   ← int_number  = 32 (dummy, IRQ0)
 *   kstack + KSTACK_SIZE - 52   ← pusha block (32 B, all zeros)
 *   kstack + KSTACK_SIZE - 56   ← DS = 0x10 (kernel data)
 *   kstack + KSTACK_SIZE - 60   ← ES = 0x10
 *   kstack + KSTACK_SIZE - 64   ← FS = 0x10
 *   kstack + KSTACK_SIZE - 68   ← GS = 0x10
 *                                ← saved_esp here
 *
 * User-mode (ring 3) stack layout for a new process:
 *
 *   kstack + KSTACK_SIZE -  4   ← SS = 0x23 (user data, RPL=3)
 *   kstack + KSTACK_SIZE -  8   ← user ESP = USER_STACK_TOP
 *   kstack + KSTACK_SIZE - 12   ← EFLAGS (IF=1)
 *   kstack + KSTACK_SIZE - 16   ← CS = 0x1B (user code, RPL=3)
 *   kstack + KSTACK_SIZE - 20   ← EIP = user entry point
 *   kstack + KSTACK_SIZE - 24   ← error_code = 0
 *   kstack + KSTACK_SIZE - 28   ← int_number = 32
 *   kstack + KSTACK_SIZE - 60   ← pusha block (32 B, all zeros)
 *   kstack + KSTACK_SIZE - 64   ← DS = 0x23 (user data)
 *   kstack + KSTACK_SIZE - 68   ← ES = 0x23
 *   kstack + KSTACK_SIZE - 72   ← FS = 0x23
 *   kstack + KSTACK_SIZE - 76   ← GS = 0x23
 *                                ← saved_esp here
 *
 * On restore: set esp = saved_esp, pop gs/fs/es/ds, popa, add esp,8, iret.
 * For ring-0: iret pops EIP/CS/EFLAGS (3 dwords).
 * For ring-3: iret also pops user ESP/SS (5 dwords total – CPU detects
 *             the privilege change from CS RPL bits).
 * ========================================================================= */

#include "process.h"
#include "descriptor.h"
#include "paging.h"
#include "pfa.h"
#include "kheap.h"
#include "string.h"
#include "log.h"
#include "sched.h"

/* -------------------------------------------------------------------------
 * User-mode segment selectors with RPL=3
 * ------------------------------------------------------------------------- */
#define USER_CS  (GDT_USER_CODE_SELECTOR | 3)   /* 0x1B */
#define USER_DS  (GDT_USER_DATA_SELECTOR | 3)   /* 0x23 */

/* -------------------------------------------------------------------------
 * Module-private state
 * ------------------------------------------------------------------------- */
static unsigned int next_pid = 1;

/* Simple process table for find/wait. */
static process_t *proc_table[PROC_MAX];
static unsigned int proc_count = 0;

static void register_process(process_t *p)
{
    if (proc_count < PROC_MAX) {
        proc_table[proc_count++] = p;
    }
}

/* -------------------------------------------------------------------------
 * process_init
 * ------------------------------------------------------------------------- */
void process_init(void)
{
    next_pid = 1;
    proc_count = 0;
    memset(proc_table, 0, sizeof(proc_table));
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
 * process_find
 * ------------------------------------------------------------------------- */
process_t *process_find(unsigned int pid)
{
    unsigned int i;
    for (i = 0; i < proc_count; i++) {
        if (proc_table[i] && proc_table[i]->pid == pid) {
            return proc_table[i];
        }
    }
    return (process_t *)0;
}

/* -------------------------------------------------------------------------
 * process_wait – busy-wait (with hlt) until the child process exits.
 * Returns the exit status, or -1 if the child doesn't exist.
 * ------------------------------------------------------------------------- */
int process_wait(unsigned int child_pid)
{
    process_t *child = process_find(child_pid);
    if (!child) return -1;

    /* If the child is already dead, return immediately. */
    if (child->state == PROC_DEAD) {
        child->waited = 1;
        return child->exit_status;
    }

    /* Put the calling process to sleep until the child exits.
     * The scheduler (sched_tick) detects WAIT_CHILD processes when a
     * child dies and wakes the parent by moving it to the run queue. */
    process_t *cur = sched_current();
    if (cur) {
        cur->wait_child_pid = child_pid;
        __asm__ volatile("sti");
        for (;;) {
            if (child->state == PROC_DEAD) break;
            cur->state       = PROC_SLEEPING;
            cur->wait_reason = WAIT_CHILD;
            __asm__ volatile("hlt");
        }
        cur->wait_reason   = WAIT_NONE;
        cur->wait_child_pid = 0;
    } else {
        /* Called from the kernel main thread (not a scheduled process).
         * Busy-wait with sti;hlt – sched_tick handles the kernel_wait_esp. */
        while (child->state != PROC_DEAD) {
            __asm__ volatile("sti; hlt");
        }
    }

    child->waited = 1;
    return child->exit_status;
}

/* -------------------------------------------------------------------------
 * process_destroy – free a dead process's resources.
 * ------------------------------------------------------------------------- */
void process_destroy(process_t *proc)
{
    unsigned int i;
    if (!proc) return;

    /* Free user page directory and its physical pages. */
    if (proc->page_dir) {
        paging_destroy_user_directory(proc->page_dir);
        proc->page_dir = (unsigned int *)0;
    }

    /* Free kernel stack. */
    if (proc->kstack) {
        kfree(proc->kstack);
        proc->kstack = (unsigned char *)0;
    }

    /* Remove from proc_table. */
    for (i = 0; i < proc_count; i++) {
        if (proc_table[i] == proc) {
            proc_table[i] = (process_t *)0;
            break;
        }
    }

    kfree(proc);
}

/* -------------------------------------------------------------------------
 * process_create – create a kernel-mode (ring 0) process.
 *
 * Builds the initial kernel stack frame including segment register saves
 * so common_isr_stub's restore sequence (pop gs/fs/es/ds + popa + iret)
 * works correctly.
 * ------------------------------------------------------------------------- */
process_t *process_create(const char *name, void (*entry)(void), int nice)
{
    process_t    *proc;
    unsigned int  kstack_top;
    unsigned int *sp;

    /* Allocate the process descriptor. */
    proc = (process_t *)kmalloc(sizeof(process_t));
    if (!proc) {
        log_error("[process] failed to allocate descriptor for '%s'", name);
        return (process_t *)0;
    }

    /* Allocate the kernel stack. */
    proc->kstack = (unsigned char *)kmalloc(PROC_KSTACK_SIZE);
    if (!proc->kstack) {
        kfree(proc);
        log_error("[process] failed to allocate kstack for '%s'", name);
        return (process_t *)0;
    }

    memset(proc->kstack, 0, PROC_KSTACK_SIZE);

    /* Fill in the descriptor. */
    proc->pid       = next_pid++;
    proc->name      = name;
    proc->nice      = nice;
    proc->weight    = nice_to_weight_val(nice);
    proc->vruntime  = 0;
    proc->state     = PROC_RUNNABLE;
    proc->next      = (process_t *)0;
    proc->is_user   = 0;
    proc->page_dir  = (unsigned int *)0;
    proc->exit_status = 0;
    proc->parent_pid  = 0;
    proc->waited      = 0;
    proc->wait_child_pid = 0;
    proc->killed    = 0;

    /*
     * Build the initial stack frame.
     *
     * Frame layout (addresses decreasing from kstack_top):
     *   [kstack_top -  4]  EFLAGS = 0x0202  (IF=1)
     *   [kstack_top -  8]  CS     = 0x0008  (kernel code)
     *   [kstack_top - 12]  EIP    = entry
     *   [kstack_top - 16]  error_code = 0
     *   [kstack_top - 20]  int_num    = 32  (dummy IRQ0)
     *   [kstack_top - 52]  pusha block (8 × 4 = 32 bytes, zeroed)
     *   [kstack_top - 56]  DS = 0x10  (kernel data)
     *   [kstack_top - 60]  ES = 0x10
     *   [kstack_top - 64]  FS = 0x10
     *   [kstack_top - 68]  GS = 0x10
     *
     *   saved_esp = kstack_top - 68  (→ GS slot)
     */
    kstack_top = (unsigned int)(proc->kstack + PROC_KSTACK_SIZE);
    sp = (unsigned int *)(kstack_top - 4);

    sp[ 0] = 0x00000202u;                           /* EFLAGS: IF=1          */
    sp[-1] = GDT_KERNEL_CODE_SELECTOR;              /* CS: 0x08              */
    sp[-2] = (unsigned int)entry;                    /* EIP                   */
    sp[-3] = 0u;                                     /* error_code            */
    sp[-4] = 32u;                                    /* int_num (dummy IRQ0)  */
    /* sp[-5] through sp[-12]: pusha block, already zero from memset */
    sp[-13] = GDT_KERNEL_DATA_SELECTOR;              /* DS: 0x10              */
    sp[-14] = GDT_KERNEL_DATA_SELECTOR;              /* ES: 0x10              */
    sp[-15] = GDT_KERNEL_DATA_SELECTOR;              /* FS: 0x10              */
    sp[-16] = GDT_KERNEL_DATA_SELECTOR;              /* GS: 0x10              */

    proc->saved_esp = kstack_top - 68u;

    register_process(proc);

    log_info("[process] created kernel '%s' pid=%d nice=%d entry=0x%x",
             name, (int)proc->pid, nice, (unsigned int)entry);

    return proc;
}

/* -------------------------------------------------------------------------
 * process_create_user – create a user-mode (ring 3) process.
 *
 * 1. Allocates process descriptor and kernel stack.
 * 2. Creates a per-process page directory (clones kernel PDs).
 * 3. Allocates physical frames for user code and stack, maps them with
 *    PTE_USER at USER_CODE_VADDR and USER_STACK_PAGE.
 * 4. Copies the user code into the mapped code page(s).
 * 5. Builds a ring-3 iret frame on the kernel stack.
 * ------------------------------------------------------------------------- */
process_t *process_create_user(const char *name,
                               const void *code, unsigned int code_size,
                               unsigned int entry_off, int nice)
{
    process_t    *proc;
    unsigned int  kstack_top;
    unsigned int *sp;
    unsigned int *pd;
    unsigned int  code_phys, stack_phys;
    unsigned int  pages_needed, i;
    unsigned int  phys;
    unsigned char *code_virt;

    /* --- Allocate descriptor + kernel stack ----------------------------- */
    proc = (process_t *)kmalloc(sizeof(process_t));
    if (!proc) {
        log_error("[process] user: failed to allocate descriptor for '%s'", name);
        return (process_t *)0;
    }

    proc->kstack = (unsigned char *)kmalloc(PROC_KSTACK_SIZE);
    if (!proc->kstack) {
        kfree(proc);
        log_error("[process] user: failed to allocate kstack for '%s'", name);
        return (process_t *)0;
    }
    memset(proc->kstack, 0, PROC_KSTACK_SIZE);

    /* --- Create page directory ------------------------------------------ */
    pd = paging_create_user_directory();
    if (!pd) {
        kfree(proc->kstack);
        kfree(proc);
        log_error("[process] user: failed to create page dir for '%s'", name);
        return (process_t *)0;
    }

    /* --- Map user code pages at USER_CODE_VADDR ------------------------- */
    pages_needed = (code_size + PAGE_SIZE_4KB - 1) / PAGE_SIZE_4KB;
    if (pages_needed == 0) pages_needed = 1;

    for (i = 0; i < pages_needed; i++) {
        phys = pfa_alloc_frame();
        if (phys == PFA_ALLOC_FAIL) {
            paging_destroy_user_directory(pd);
            kfree(proc->kstack);
            kfree(proc);
            log_error("[process] user: out of frames for code of '%s'", name);
            return (process_t *)0;
        }
        paging_map_page(pd, USER_CODE_VADDR + i * PAGE_SIZE_4KB, phys,
                         PTE_PRESENT | PTE_USER | PTE_RW);

        /* Copy code into the physical frame (via kernel mapping). */
        code_virt = (unsigned char *)PHYS_TO_VIRT(phys);
        {
            unsigned int offset = i * PAGE_SIZE_4KB;
            unsigned int chunk  = code_size - offset;
            if (chunk > PAGE_SIZE_4KB) chunk = PAGE_SIZE_4KB;
            memcpy(code_virt, (const unsigned char *)code + offset, chunk);
            /* Zero the remainder of the page. */
            if (chunk < PAGE_SIZE_4KB) {
                memset(code_virt + chunk, 0, PAGE_SIZE_4KB - chunk);
            }
        }
    }
    (void)code_phys; /* suppress unused warning */

    /* --- Map user stack page at USER_STACK_PAGE ------------------------- */
    stack_phys = pfa_alloc_frame();
    if (stack_phys == PFA_ALLOC_FAIL) {
        paging_destroy_user_directory(pd);
        kfree(proc->kstack);
        kfree(proc);
        log_error("[process] user: out of frames for stack of '%s'", name);
        return (process_t *)0;
    }
    paging_map_page(pd, USER_STACK_PAGE, stack_phys,
                     PTE_PRESENT | PTE_USER | PTE_RW);
    /* Zero the stack page. */
    memset((void *)PHYS_TO_VIRT(stack_phys), 0, PAGE_SIZE_4KB);

    /* --- Fill in the descriptor ----------------------------------------- */
    proc->pid         = next_pid++;
    proc->name        = name;
    proc->nice        = nice;
    proc->weight      = nice_to_weight_val(nice);
    proc->vruntime    = 0;
    proc->state       = PROC_RUNNABLE;
    proc->next        = (process_t *)0;
    proc->is_user     = 1;
    proc->page_dir    = pd;
    proc->exit_status = 0;
    proc->parent_pid  = 0;
    proc->waited      = 0;
    proc->wait_child_pid = 0;
    proc->killed      = 0;

    /*
     * Build the initial ring-3 iret frame on the kernel stack.
     *
     * Frame layout (addresses decreasing from kstack_top):
     *   [kstack_top -  4]  SS     = 0x23 (user data, RPL=3)
     *   [kstack_top -  8]  ESP    = USER_STACK_TOP
     *   [kstack_top - 12]  EFLAGS = 0x0202 (IF=1)
     *   [kstack_top - 16]  CS     = 0x1B (user code, RPL=3)
     *   [kstack_top - 20]  EIP    = USER_CODE_VADDR + entry_off
     *   [kstack_top - 24]  error_code = 0
     *   [kstack_top - 28]  int_num    = 32
     *   [kstack_top - 60]  pusha block (32 B, zeroed)
     *   [kstack_top - 64]  DS = 0x23
     *   [kstack_top - 68]  ES = 0x23
     *   [kstack_top - 72]  FS = 0x23
     *   [kstack_top - 76]  GS = 0x23
     *
     *   saved_esp = kstack_top - 76
     */
    kstack_top = (unsigned int)(proc->kstack + PROC_KSTACK_SIZE);
    sp = (unsigned int *)(kstack_top - 4);

    sp[ 0] = USER_DS;                               /* SS:  0x23             */
    sp[-1] = USER_STACK_TOP;                         /* user ESP              */
    sp[-2] = 0x00000202u;                            /* EFLAGS: IF=1          */
    sp[-3] = USER_CS;                                /* CS:  0x1B             */
    sp[-4] = USER_CODE_VADDR + entry_off;            /* EIP: user entry       */
    sp[-5] = 0u;                                     /* error_code            */
    sp[-6] = 32u;                                    /* int_num               */
    /* sp[-7] through sp[-14]: pusha block, already zero */
    sp[-15] = USER_DS;                               /* DS: 0x23              */
    sp[-16] = USER_DS;                               /* ES: 0x23              */
    sp[-17] = USER_DS;                               /* FS: 0x23              */
    sp[-18] = USER_DS;                               /* GS: 0x23              */

    proc->saved_esp = kstack_top - 76u;

    register_process(proc);

    log_info("[process] created user '%s' pid=%d nice=%d entry=0x%x pd=0x%x",
             name, (int)proc->pid, nice,
             USER_CODE_VADDR + entry_off, (unsigned int)pd);
    log_debug("[process] iret frame: saved_esp=0x%x user_esp=0x%x eip=0x%x cs=0x%x",
              proc->saved_esp, (unsigned int)sp[-1],
              (unsigned int)sp[-4], (unsigned int)sp[-3]);

    return proc;
}
