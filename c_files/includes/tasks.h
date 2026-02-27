#ifndef TASKS_H
#define TASKS_H

/* =========================================================================
 * tasks.h – Kernel-mode test tasks for multitasking demonstration
 *
 * Three tasks with different nice values are spawned by kmain() to
 * demonstrate the CFS scheduler in action.  Each task writes a live
 * counter to a dedicated row on the VGA framebuffer so that the relative
 * CPU allocation is visible in real time.
 *
 *   task_a  nice = -5  (high priority, weight=3121)  → more CPU time
 *   task_b  nice =  0  (normal,        weight=1024)  → baseline
 *   task_c  nice = +5  (low priority,  weight= 335)  → less CPU time
 *   task_idle nice = +19 (background,  weight=  15)  → minimal CPU
 *
 * The difference in counter speeds on screen reflects the CFS weighting.
 * ========================================================================= */

/** Entry points for the test tasks. Each runs as an infinite loop. */
void task_a(void);   /* nice = -5  high priority  */
void task_b(void);   /* nice =  0  normal          */
void task_c(void);   /* nice = +5  low priority    */
void task_idle(void);/* nice = +19 idle/background */

#endif /* TASKS_H */
