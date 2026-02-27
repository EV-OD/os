#ifndef PROCESS_H
#define PROCESS_H

/* =========================================================================
 * process.h – Process / task descriptor for the CFS scheduler
 *
 * Each process is a kernel thread (ring 0) with its own kernel stack.
 * User-mode processes will add a separate user stack and page directory
 * once full user-mode support is implemented.
 *
 * CFS vruntime model
 * ------------------
 * Each process tracks a virtual runtime (vruntime) which increases faster
 * for low-priority tasks (high nice value) and slower for high-priority tasks
 * (low nice / negative nice).
 *
 *   vruntime_delta = tick_ms * NICE0_WEIGHT / process_weight
 *   vruntime      += vruntime_delta  (on each timer tick for the running proc)
 *
 * The scheduler always picks the RUNNABLE process with the smallest vruntime.
 * When a new process is added its vruntime is set to the current minimum
 * vruntime so it is not artificially preferred over longer-running processes.
 *
 * Nice value → weight mapping (simplified Linux table, nice -20..+19):
 *   nice ≤ -20 → weight 88761  (runs ~87× more than nice 19)
 *   nice   0   → weight  1024  (baseline)
 *   nice ≥ 19  → weight    15  (runs ~87× less than nice -20)
 *
 * Reference: Linux kernel sched/fair.c, Documentation/scheduler/sched-design-CFS.rst
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

/** Maximum number of simultaneously-existing processes. */
#define PROC_MAX         16u

/** Kernel stack size per process (bytes).  4 KB is standard. */
#define PROC_KSTACK_SIZE 4096u

/** CFS weight for nice 0 (baseline). */
#define NICE0_WEIGHT     1024u

/** Nice value range. */
#define NICE_MIN  (-20)
#define NICE_MAX  (19)

/* -------------------------------------------------------------------------
 * Process state machine
 *
 *  RUNNABLE ←→ RUNNING
 *  RUNNING  →  SLEEPING  (yield/block)
 *  SLEEPING →  RUNNABLE  (wake-up)
 *  RUNNING  →  DEAD      (exit / returned from task function)
 * ------------------------------------------------------------------------- */
typedef enum proc_state {
    PROC_RUNNABLE = 0,   /**< Ready to run; in the CFS run queue     */
    PROC_RUNNING  = 1,   /**< Currently executing on the CPU          */
    PROC_SLEEPING = 2,   /**< Blocked / waiting (not in run queue)    */
    PROC_DEAD     = 3    /**< Exited; slot may be reclaimed           */
} proc_state_t;

/* -------------------------------------------------------------------------
 * Process descriptor
 *
 * Allocated via kmalloc().  Linked into the CFS run queue by the scheduler.
 * ------------------------------------------------------------------------- */
typedef struct process {
    /* --- Identity -------------------------------------------------------- */
    unsigned int    pid;          /**< Unique process ID (1-based)           */
    const char     *name;         /**< Human-readable name (for logging)     */

    /* --- CFS scheduling fields ------------------------------------------- */
    int             nice;         /**< Nice value: -20 (highest) … +19 (lowest) */
    unsigned int    weight;       /**< CFS weight derived from nice           */
    unsigned int    vruntime;     /**< Virtual runtime (ms-equivalent units)  */

    /* --- State ------------------------------------------------------------ */
    proc_state_t    state;        /**< Current state of this process          */

    /* --- Kernel stack and saved context ----------------------------------- */
    unsigned char  *kstack;       /**< Base (lowest address) of kernel stack  */
    unsigned int    saved_esp;    /**< Kernel ESP saved at last context switch */
                                  /* Points to the pusha block on kstack       */

    /* --- Linked list for run queue --------------------------------------- */
    struct process *next;         /**< Next process in the run queue (or NULL)*/
} process_t;

/* -------------------------------------------------------------------------
 * CFS nice-to-weight table (indices 0..39 → nice -20..+19)
 * Derived from Linux kernel sched/core.c prio_to_weight[].
 * ------------------------------------------------------------------------- */
static const unsigned int nice_to_weight[40] = {
    /* nice -20 */ 88761, 71755, 56483, 46273, 36291,
    /* nice -15 */ 29154, 23254, 18705, 14949, 11916,
    /* nice -10 */  9548,  7620,  6100,  4904,  3906,
    /* nice  -5 */  3121,  2501,  1991,  1586,  1277,
    /* nice   0 */  1024,   820,   655,   526,   423,
    /* nice  +5 */   335,   272,   215,   172,   137,
    /* nice +10 */   110,    87,    70,    56,    45,
    /* nice +15 */    36,    29,    23,    18,    15,
};

/* Clamp a nice value to [-20, +19] and return the corresponding weight. */
static inline unsigned int nice_to_weight_val(int nice)
{
    if (nice < NICE_MIN) nice = NICE_MIN;
    if (nice > NICE_MAX) nice = NICE_MAX;
    return nice_to_weight[(unsigned int)(nice + 20)];
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/**
 * process_init – initialise the process subsystem.
 * Must be called before any process_create_*() calls.
 */
void process_init(void);

/**
 * process_create – allocate a new process with a kernel-mode entry point.
 *
 * Sets up a fake interrupt-return frame on the new process's kernel stack so
 * the scheduler can restore it via popa + iret.
 *
 * @param name       Human-readable name (used only in log output).
 * @param entry      C function to run as the process body (void fn(void)).
 * @param nice       Nice value in [-20, +19].
 *
 * @return  Pointer to the new process_t, or NULL on allocation failure.
 */
process_t *process_create(const char *name, void (*entry)(void), int nice);

/**
 * process_get_next_pid – return the next available PID (auto-incremented).
 */
unsigned int process_get_next_pid(void);

#endif /* PROCESS_H */
