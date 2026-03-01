#ifndef PROCESS_H
#define PROCESS_H

/* =========================================================================
 * process.h – Process / task descriptor for the CFS scheduler
 *
 * Each process has its own kernel stack for interrupt handling.
 * Kernel-mode processes (ring 0) run entirely on that stack.
 * User-mode processes (ring 3) additionally have a user page directory
 * and a user-space stack; on interrupts the CPU switches to the kernel
 * stack via TSS.esp0.
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
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

/** Maximum number of simultaneously-existing processes. */
#define PROC_MAX         16u

/** Kernel stack size per process (bytes).  32 KB prevents deep-call overflow. */
#define PROC_KSTACK_SIZE 32768u

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

/** Reason a process is in PROC_SLEEPING state. */
typedef enum wait_reason {
    WAIT_NONE  = 0,  /**< Not sleeping / unspecified              */
    WAIT_KEY   = 1,  /**< Sleeping until a keyboard char arrives  */
    WAIT_CHILD = 2   /**< Sleeping until a child process exits    */
} wait_reason_t;

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
    wait_reason_t   wait_reason;  /**< Why the process is sleeping (WAIT_*)   */

    /* --- Kernel stack and saved context ----------------------------------- */
    unsigned char  *kstack;       /**< Base (lowest address) of kernel stack  */
    unsigned int    saved_esp;    /**< Kernel ESP saved at last context switch */
                                  /* Points to the seg-save + pusha block     */

    /* --- User-mode fields ------------------------------------------------ */
    int             is_user;      /**< 1 = ring-3 user process, 0 = kernel   */
    unsigned int   *page_dir;     /**< User page directory (kernel-virt ptr)  */
                                  /* NULL for kernel-mode processes            */
    int             exit_status;  /**< Exit code set by SYS_EXIT              */

    /* --- Parent / wait relationship -------------------------------------- */
    unsigned int    parent_pid;   /**< PID of the process that spawned this   */
    int             waited;       /**< 1 = parent has collected exit_status   */
    unsigned int    wait_child_pid; /**< PID of child being waited on (WAIT_CHILD) */

    /* --- Signal flags ---------------------------------------------------- */
    volatile int    killed;       /**< Non-zero: process should terminate ASAP */

    /* --- Command-line arguments (user processes) ------------------------ */
    int             argc;         /**< Number of args (0 if unused)           */
    const char     *args[8];      /**< Argument strings (kernel-owned copies) */

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
 * process_create – allocate a new kernel-mode (ring 0) process.
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
 * process_create_user – allocate a new user-mode (ring 3) process.
 *
 * Creates a per-process page directory, maps user code at USER_CODE_VADDR,
 * maps a user stack page, and builds a ring-3 iret frame (5-dword:
 * EIP, CS=0x1B, EFLAGS, ESP, SS=0x23) on the kernel stack.
 *
 * @param name       Human-readable name.
 * @param code       Pointer to the user code buffer (kernel-virtual).
 * @param code_size  Size of the code in bytes.
 * @param entry_off  Offset within code to the entry point.
 * @param nice       Nice value in [-20, +19].
 *
 * @return  Pointer to the new process_t, or NULL on failure.
 */
process_t *process_create_user(const char *name,
                               const void *code, unsigned int code_size,
                               unsigned int entry_off, int nice);

/**
 * process_get_next_pid – return the next available PID (auto-incremented).
 */
unsigned int process_get_next_pid(void);

/**
 * process_find – look up a process by PID.
 * Scans the scheduler's run queue and the dead-list.
 *
 * @return  Pointer to the process, or NULL if not found.
 */
process_t *process_find(unsigned int pid);

/**
 * process_wait – block the calling process until a child exits.
 * Returns the child's exit status, or -1 if the child does not exist.
 * For now, busy-waits (polling) until the child enters PROC_DEAD state.
 *
 * @param child_pid  PID of the child process to wait for.
 * @return           Exit status of the child.
 */
int process_wait(unsigned int child_pid);

/**
 * process_destroy – free a dead process's resources (kstack, page dir, etc.).
 */
void process_destroy(process_t *proc);

#endif /* PROCESS_H */
