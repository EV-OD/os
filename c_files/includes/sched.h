#ifndef SCHED_H
#define SCHED_H

/* =========================================================================
 * sched.h – Completely Fair Scheduler (CFS) with preemptive tick-based
 *            context switching
 *
 * Overview
 * --------
 * This is a simplified Linux-style CFS scheduler for a single-CPU kernel.
 * Each runnable process has a `vruntime` (virtual runtime) which grows on
 * every timer tick according to the process's CFS weight:
 *
 *   vruntime_delta = PIT_TICK_MS * NICE0_WEIGHT / process.weight
 *
 * The scheduler always picks the runnable process with the minimum vruntime.
 * This naturally gives more CPU time to high-priority (low nice / high weight)
 * processes while still allowing all processes to make forward progress.
 *
 * New processes enter the queue with vruntime = current min_vruntime so they
 * are not unfairly preferred (or starved) when they first run.
 *
 * Context switch mechanism
 * ------------------------
 * Context switching is driven by the PIT IRQ0 handler which calls
 * sched_tick() on every timer interrupt.  sched_tick() returns the new
 * kernel-stack ESP (the saved_esp of the next process) or 0 for no switch.
 * The modified common_isr_stub in isr.s checks this return value and
 * switches ESP before executing popa + iret.
 *
 * API summary
 * -----------
 *   sched_init()   – call once after process_init() and pit_init()
 *   sched_add()    – enqueue a new process
 *   sched_start()  – launch the scheduler (never returns)
 *   sched_tick()   – called from interrupt_handler for every IRQ0
 * ========================================================================= */

#include "process.h"
#include "idt.h"

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/**
 * sched_init – initialise the scheduler run queue.
 * Must be called once before sched_add() or sched_start().
 */
void sched_init(void);

/**
 * sched_add – add a process to the CFS run queue.
 *
 * Sets the process's vruntime to the current minimum vruntime of all
 * processes in the queue so it is treated fairly on its first time slice.
 *
 * @param proc  Process descriptor (from process_create()).
 */
void sched_add(process_t *proc);

/**
 * sched_start – enter the scheduler and start running processes.
 *
 * Picks the process with the lowest vruntime, sets it RUNNING, and
 * uses inline assembly to restore its saved context via iret.
 *
 * NEVER RETURNS to the caller.
 * Must be called with interrupts DISABLED; the iret will re-enable them
 * (because the restored EFLAGS has IF=1).
 */
void sched_start(void);

/**
 * sched_tick – called from interrupt_handler() on every PIT IRQ0.
 *
 * 1. Increments the PIT tick counter.
 * 2. Updates the current process's vruntime.
 * 3. Re-inserts the current process into the sorted run queue.
 * 4. Picks the next process (lowest vruntime).
 * 5. If a switch is needed: updates TSS.esp0, saves current ESP, and
 *    returns the new process's saved_esp.
 *
 * @param cpu        Pointer to the pusha register block on the kernel stack.
 *                   This IS the saved_esp for the interrupted process.
 * @param interrupt  Interrupt vector number (must be 32 for PIT).
 *
 * @return  Non-zero: new kernel ESP (switch to that process).
 *          Zero:     stay in the current process (no switch needed).
 */
unsigned int sched_tick(struct cpu_state *cpu, unsigned int interrupt);

/**
 * sched_current – return a pointer to the currently running process.
 */
process_t *sched_current(void);

/**
 * sched_dump – log the current state of the run queue (for debugging).
 */
void sched_dump(void);

#endif /* SCHED_H */
