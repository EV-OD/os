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
#include "log.h"

/* -------------------------------------------------------------------------
 * Module-private state
 * ------------------------------------------------------------------------- */
static process_t *run_queue    = (process_t *)0;   /* sorted run queue head  */
static process_t *current_proc = (process_t *)0;   /* currently running task */
static unsigned int tick_total = 0;                 /* total timer ticks seen */

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
     * if a ring-3 process is interrupted the CPU uses the right stack.
     * (For ring-0 kernel tasks the TSS isn't used for the switch, but
     * set it correctly for when user-mode is added later.) */
    tss_set_kernel_stack((unsigned int)(first->kstack + PROC_KSTACK_SIZE));

    esp = first->saved_esp;

    log_info("[sched] starting '%s' pid=%d esp=0x%x",
             first->name, (int)first->pid, esp);

    /*
     * Restore the process's saved context and enter it via iret.
     *
     * Stack at esp (built by process_create):
     *   [esp+0..28] = pusha block (8 registers, zeroed for new procs)
     *   [esp+32]    = dummy int number (32)
     *   [esp+36]    = dummy error code (0)
     *   [esp+40]    = EIP  (entry_point)
     *   [esp+44]    = CS   (0x08)
     *   [esp+48]    = EFLAGS (0x202, IF=1)
     *
     * Sequence:
     *   1. Set ESP = first->saved_esp  (switch to process's kernel stack)
     *   2. popa                        (restore 8 GP registers)
     *   3. add $8, esp                 (skip error_code + int_number)
     *   4. iret                        (pop EIP, CS, EFLAGS → enter process)
     */
    __asm__ volatile(
        "mov %0, %%esp\n\t"    /* 1. switch stacks               */
        "popa\n\t"              /* 2. restore GP registers        */
        "add $8, %%esp\n\t"    /* 3. skip dummy error+int_num    */
        "iret"                  /* 4. enter the process           */
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

    /* If no process is currently running (should not happen), do nothing. */
    if (!current_proc) {
        return 0;
    }

    /*
     * Save the current process's kernel stack pointer.
     * cpu IS the pointer to the pusha block on the kernel stack,
     * which is exactly saved_esp.
     */
    current_proc->saved_esp = (unsigned int)cpu;

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

    /*
     * Context switch to `next`.
     * Update TSS.esp0 so the CPU loads the right kernel stack on the
     * next ring-0 entry (important when user-mode is added later).
     */
    tss_set_kernel_stack((unsigned int)(next->kstack + PROC_KSTACK_SIZE));

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
