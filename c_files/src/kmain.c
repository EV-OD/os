/* =========================================================================
 * kmain.c – Kernel entry point (called from loader.s after paging is up)
 *
 * Receives the Multiboot magic and info pointer from the bootloader,
 * initialises all subsystems via kernel_init(), then runs the rosc
 * compiler demo.  See docs/kernel/boot_sequence.md.
 * ========================================================================= */

#include "kernel_init.h"
#include "display.h"
#include "stdio.h"
#include "module.h"
#include "rosc.h"

void kmain(unsigned int eax, unsigned int ebx)
{
    /* Suppress unused-parameter warnings; parameters are not used while
     * the rosc compiler shell is the primary entry point (Phase 1). */
    (void)eax;
    (void)ebx;

    kernel_init();
    fb_clear();
    cursor_move_home();
    display_boot_info();

    /* Run the built-in rosc compiler shell (Phase 1).
     * No file system is needed — the test source is hardcoded inside rosc.c.
     * module_run() is intentionally skipped for now; re-enable it once the
     * compiler is migrated to load source files from disk. */
    rosc_run();

    /* Halt after the compiler shell returns */
    while (1) {}
}
