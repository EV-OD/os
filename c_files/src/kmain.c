/* =========================================================================
 * kmain.c – Kernel entry point (called from loader.s after paging is up)
 *
 * loader.s pushes two arguments before calling kmain:
 *   eax – Multiboot magic value  (0x2BADB002 if GRUB is the loader).
 *   ebx – Virtual address of the Multiboot information structure
 *         (loader.s adds KERNEL_VIRTUAL_BASE so C code can dereference it).
 *
 * Boot sequence
 * -------------
 *   kernel_init()  – hardware + memory subsystems (GDT…PFA…kheap)
 *   display setup  – clear framebuffer, show boot banner
 *   heap smoke test – quick kmalloc/kfree round-trip to validate the heap
 *   rosc_run()     – built-in compiler shell (Phase 1)
 *
 * See docs/kernel/boot_sequence.md for the full rationale.
 * ========================================================================= */

#include "kernel_init.h"
#include "multiboot.h"
#include "display.h"
#include "stdio.h"
#include "module.h"
#include "rosc.h"
#include "kheap.h"
#include "log.h"

void kmain(unsigned int eax, unsigned int ebx)
{
    /*
     * Forward the Multiboot magic and info pointer to kernel_init().
     * ebx is the virtual address forwarded by loader.s
     * (physical address + KERNEL_VIRTUAL_BASE).
     */
    kernel_init(eax, (multiboot_info_t *)ebx);

    fb_clear();
    cursor_move_home();
    display_boot_info();

    /* ------------------------------------------------------------------
     * Heap smoke test – allocate and free a few blocks to confirm the
     * allocator and magic-number guard are working correctly.
     * ------------------------------------------------------------------ */
    {
        void *a = kmalloc(64);
        void *b = kmalloc(128);
        void *c = kmalloc(32);
        log_info("[kmain] heap smoke test: a=0x%x b=0x%x c=0x%x",
                 (unsigned int)a, (unsigned int)b, (unsigned int)c);
        kfree(b);               /* free middle → coalesce test            */
        kfree(a);               /* free left   → should coalesce with gap */
        kfree(c);               /* free right  → heap should be full again*/
        log_info("[kmain] heap smoke test passed");
    }

    /* Run the built-in rosc compiler shell (Phase 1).
     * No file system is needed — the test source is hardcoded inside rosc.c.
     * module_run() is intentionally skipped for now; re-enable it once the
     * compiler is migrated to load source files from disk. */
    rosc_run();

    /* Halt after the compiler shell returns */
    while (1) {}
}
