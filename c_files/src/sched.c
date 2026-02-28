/* =========================================================================
 * sched.c – Completely Fair Scheduler (CFS) with preemptive scheduling
 *
 * Run-queue design
 * ----------------
 * A singly-linked list sorted ASCENDING by vruntime acts as the run queue.
 * The process with the MINIMUM vruntime is always at the head – O(1) pick-next,
 * O(n) insert (n ≤ PROC_MAX = 16, so this is fine).
 *
 * Invariants
 * ----------
 * •  The current_proc is NOT in the run_queue while it is RUNNING.
 * •  On each timer tick the current_proc is re-inserted (state=RUNNABLE),
 *    the minimum-vruntime process is dequeued and made RUNNING.
 * •  If the re-inserted current_proc is still the minimum it is dequeued
 *    again immediately and no stack switch takes place (return 0).
 *
 * vruntime accounting
 * -------------------
 *   delta  = PIT_TICK_MS * NICE0_WEIGHT / process.weight
 *   vruntime += delta
 * Higher weight (more negative nice) → smaller delta → smaller vruntime
 * increase → process gets more CPU time (CFS picks smallest vruntime).
 * ========================================================================= */

#include "sched.h"
#include "process.h"
#include "pit.h"
#include "tss.h"
#include "paging.h"
#include "log.h"

/* -------------------------------------------------------------------------
 * Module-private state
 * ------------------------------------------------------------------------- */
static process_t *run_queue    = (process_t *)0;   /* sorted run queue head  */
static process_t *current_proc = (process_t *)0;   /* currently running task */
static unsigned int tick_total = 0;                 /* total timer ticks seen */

/*
 * Saved kernel-main-thread interrupt-frame ESP.
 *
 * When process_wait() is called from the kernel main thread (which is NOT
 * a scheduled process), we loop with sti;hlt.  When the PIT fires and
 * current_proc is NULL, sched_tick() saves the kernel interrupt frame here
 * before switching to a queued process.  When all scheduled processes are
 * dead, we restore this ESP to return to the hlt in process_wait().
 */
static unsigned int kernel_wait_esp = 0;

/* -------------------------------------------------------------------------
 * enqueue_sorted – insert proc into run_queue sorted ascending by vruntime.
 * ------------------------------------------------------------------------- */
static void enqueue_sorted(process_t *proc)
{
    process_t **pp;

    proc->state = PROC_RUNNABLE;
    proc->next  = (process_t *)0;

    /* Find insertion point: keep queue ascending by vruntime. */
    pp = &run_queue;
    while (*pp && ((*pp)->vruntime <= proc->vruntime)) {
        pp = &(*pp)->next;
    }
    proc->next = *pp;
    *pp = proc;
}

/* -------------------------------------------------------------------------
 * dequeue_min – remove and return the front of the run_queue (min vruntime).
 * Returns NULL if the queue is empty.
 * ------------------------------------------------------------------------- */
static process_t *dequeue_min(void)
{
    process_t *p = run_queue;
    if (p) {
        run_queue = p->next;
        p->next   = (process_t *)0;
    }
    return p;
}

/* -------------------------------------------------------------------------
 * min_vruntime – return the minimum vruntime across all processes in the
 * run queue.  Used when adding a new process to avoid starvation.
 * ------------------------------------------------------------------------- */
static unsigned int min_vruntime(void)
{
    unsigned int min = 0xFFFFFFFFu;
    process_t   *p   = run_queue;

    /* Include the current process if it is running. */
    if (current_proc) {
        min = current_proc->vruntime;
    }

    while (p) {
        if (p->vruntime < min) {
            min = p->vruntime;
        }
        p = p->next;
    }

    return (min == 0xFFFFFFFFu) ? 0 : min;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * sched_init
 * ------------------------------------------------------------------------- */
void sched_init(void)
{
    run_queue    = (process_t *)0;
    current_proc = (process_t *)0;
    tick_total   = 0;
    log_info("[sched] CFS scheduler initialised");
}

/* -------------------------------------------------------------------------
 * sched_add
 * ------------------------------------------------------------------------- */
void sched_add(process_t *proc)
{
    if (!proc) return;

    /*
     * Set the new process's vruntime to the current minimum so it starts
     * running at a fair starting point (not ahead of everyone else).
     * A brand-new queue will give min_vruntime = 0.
     */
    proc->vruntime = min_vruntime();

    enqueue_sorted(proc);

    log_info("[sched] added '%s' pid=%d nice=%d weight=%d vruntime=%d",
             proc->name, (int)proc->pid, proc->nice,
             (int)proc->weight, (int)proc->vruntime);
}

/* -------------------------------------------------------------------------
 * sched_current
 * ------------------------------------------------------------------------- */
process_t *sched_current(void)
{
    return current_proc;
}

/* -------------------------------------------------------------------------
 * sched_dump – log run queue for debugging
 * ------------------------------------------------------------------------- */
void sched_dump(void)
{
    process_t *p = run_queue;
    int        i = 0;

    log_info("[sched] --- run queue dump (ticks=%d) ---", (int)tick_total);
    if (current_proc) {
        log_info("[sched]  RUNNING: '%s' pid=%d vruntime=%d nice=%d",
                 current_proc->name, (int)current_proc->pid,
                 (int)current_proc->vruntime, current_proc->nice);
    }
    while (p) {
        log_info("[sched]  [%d] '%s' pid=%d vruntime=%d nice=%d",
                 i, p->name, (int)p->pid, (int)p->vruntime, p->nice);
        p = p->next;
        i++;
    }
    log_info("[sched] --- end dump ---");
}

/* -------------------------------------------------------------------------
 * sched_start – iret into the first process; never returns.
 * ------------------------------------------------------------------------- */
__attribute__((noreturn))
void sched_start(void)
{
    process_t    *first;
    unsigned int  esp;

    if (!run_queue) {
        log_error("[sched] sched_start: no processes queued!");
        __asm__ volatile("cli; hlt");
        __builtin_unreachable();
    }

    /* Dequeue the process with the minimum vruntime. */
    first = dequeue_min();
    first->state  = PROC_RUNNING;
    current_proc  = first;

    /* Point TSS.esp0 at the top of this process's kernel stack so that
     * if a ring-3 process is interrupted the CPU uses the right stack. */
    tss_set_kernel_stack((unsigned int)(first->kstack + PROC_KSTACK_SIZE));

    /* Switch to user page directory if this is a user-mode process. */
    if (first->is_user && first->page_dir) {
        paging_switch_directory(first->page_dir);
    }

    esp = first->saved_esp;

    log_info("[sched] starting '%s' pid=%d esp=0x%x user=%d",
             first->name, (int)first->pid, esp, first->is_user);

    /*
     * Restore the process's saved context and enter it via iret.
     *
     * Stack at esp (built by process_create / process_create_user):
     *   [esp+ 0..15] = saved GS, FS, ES, DS  (4 × 4 = 16 bytes)
     *   [esp+16..47] = pusha block            (8 × 4 = 32 bytes)
     *   [esp+48]     = int_num
     *   [esp+52]     = error_code
     *   [esp+56]     = EIP
     *   [esp+60]     = CS
     *   [esp+64]     = EFLAGS
     *   [esp+68]     = user ESP  (ring 3 only)
     *   [esp+72]     = user SS   (ring 3 only)
     *
     * Sequence:
     *   1. Set ESP = saved_esp
     *   2. pop gs, fs, es, ds   (restore segment registers)
     *   3. popa                 (restore GP registers)
     *   4. add $8, esp          (skip int_num + error_code)
     *   5. iret                 (enter process)
     */
    __asm__ volatile(
        "mov %0, %%esp\n\t"    /* 1. switch stacks               */
        "pop %%gs\n\t"          /* 2. restore segment registers   */
        "pop %%fs\n\t"
        "pop %%es\n\t"
        "pop %%ds\n\t"
        "popa\n\t"              /* 3. restore GP registers        */
        "add $8, %%esp\n\t"    /* 4. skip int_num + error_code   */
        "iret"                  /* 5. enter the process           */
        :
        : "r"(esp)
        : "memory"
    );

    __builtin_unreachable();
}

/* -------------------------------------------------------------------------
 * sched_tick – preemptive CFS tick; called from interrupt_handler() on IRQ0.
 *
 * Returns the new kernel ESP to switch to (0 = keep current process).
 * ------------------------------------------------------------------------- */
unsigned int sched_tick(struct cpu_state *cpu, unsigned int interrupt)
{
    process_t    *next;
    unsigned int  delta;

    /* Only handle PIT timer (IRQ0 = vector 32). */
    if (interrupt != 32u) {
        return 0;
    }

    /* Advance the tick counter. */
    pit_tick();
    tick_total++;

    /*
     * No process is currently running – we are in the kernel main thread
     * (e.g. process_wait doing sti;hlt).  If there is a queued process,
     * save the kernel context and switch to it.
     */
    if (!current_proc) {
        next = dequeue_min();
        if (!next) return 0;   /* nothing queued, stay in kernel */

        /* Save the kernel main thread's interrupt-frame ESP so we can
         * return to it later (when all scheduled processes finish). */
        kernel_wait_esp = (unsigned int)cpu - 16u;

        tss_set_kernel_stack((unsigned int)(next->kstack + PROC_KSTACK_SIZE));
        if (next->is_user && next->page_dir) {
            paging_switch_directory(next->page_dir);
        }
        next->state  = PROC_RUNNING;
        current_proc = next;

        log_info("[sched] kernel→proc switch: '%s' pid=%d esp=0x%x",
                 next->name, (int)next->pid, next->saved_esp);

        return next->saved_esp;
    }

    /*
     * Save the current process's kernel stack pointer.
     * cpu points to the pusha block on the kernel stack.
     * But our new common_isr_stub also pushes 4 segment registers (16 bytes)
     * below the pusha block.  saved_esp must point to the GS slot (bottom
     * of the saved state), which is at cpu - 4 dwords = cpu - 16 bytes.
     */
    current_proc->saved_esp = (unsigned int)cpu - 16u;

    /*
     * If the current process is DEAD (called SYS_EXIT), don't re-enqueue.
     * Just pick the next process.
     */
    if (current_proc->state == PROC_DEAD) {
        next = dequeue_min();
        if (!next) {
            /*
             * No more scheduled processes.  If we have a saved kernel
             * context (from process_wait), restore it so the kernel
             * main thread can resume.
             */
            if (kernel_wait_esp) {
                unsigned int ret_esp = kernel_wait_esp;
                kernel_wait_esp = 0;
                current_proc = (process_t *)0;
                paging_switch_directory(paging_get_kernel_directory());
                log_info("[sched] all processes done, returning to kernel");
                return ret_esp;
            }
            log_error("[sched] all processes dead – halting");
            __asm__ volatile("cli; hlt");
            return 0;
        }
        goto do_switch;
    }

    /*
     * Update vruntime:
     *   delta = PIT_TICK_MS * NICE0_WEIGHT / weight
     * Higher weight (more negative nice) → smaller delta → smaller vruntime
     * increase → process gets proportionally more CPU time.
     */
    delta = (PIT_TICK_MS * NICE0_WEIGHT) / current_proc->weight;
    current_proc->vruntime += delta;

    /* Re-insert current process into the sorted run queue. */
    enqueue_sorted(current_proc);

    /* Pick the process with the minimum vruntime. */
    next = dequeue_min();

    if (!next) {
        /* Queue became empty somehow – keep running current. */
        current_proc->state = PROC_RUNNING;
        return 0;
    }

    /* If the same process was picked again, no context switch needed. */
    if (next == current_proc) {
        next->state = PROC_RUNNING;
        return 0;
    }

do_switch:
    /*
     * Context switch to `next`.
     * Update TSS.esp0 so the CPU loads the right kernel stack on the
     * next ring-0 entry (critical for user-mode processes).
     */
    tss_set_kernel_stack((unsigned int)(next->kstack + PROC_KSTACK_SIZE));

    /*
     * Switch page directories if the new process has a different one.
     * - Kernel processes use the boot page directory (NULL page_dir).
     * - User processes have their own page directory.
     * If switching between two kernel processes, no CR3 change is needed.
     */
    if (next->page_dir != current_proc->page_dir) {
        if (next->is_user && next->page_dir) {
            paging_switch_directory(next->page_dir);
        } else if (!next->is_user) {
            /* Switching back to a kernel process – restore boot PD. */
            paging_switch_directory(paging_get_kernel_directory());
        }
    }

    next->state  = PROC_RUNNING;
    current_proc = next;

    /* Log every 100 ticks to avoid flooding the serial port. */
    if ((tick_total % 100) == 0) {
        log_info("[sched] tick=%d → '%s' pid=%d vruntime=%d",
                 (int)tick_total, next->name, (int)next->pid,
                 (int)next->vruntime);
    }

    /* Return the new kernel ESP – isr.s will switch to this stack. */
    return next->saved_esp;
}
