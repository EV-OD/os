/* =========================================================================
 * tss.c – Task State Segment initialisation and runtime updates
 *
 * We maintain a single, CPU-wide TSS for software multi-tasking.
 * The only fields touched at runtime are ss0 and esp0.
 *
 * Flow:
 *   1. tss_init()             – called once from kernel_init()
 *      a. Zero the TSS.
 *      b. Set ss0 = GDT_KERNEL_DATA_SELECTOR.
 *      c. Set esp0 = initial kernel stack top (will be updated per switch).
 *      d. Call gdt_set_tss_entry() to write the descriptor into GDT[5].
 *      e. Execute `ltr` to load the task register with the TSS selector.
 *
 *   2. tss_set_kernel_stack() – called on every process switch by the
 *      scheduler to point esp0 at the top of the incoming process's kernel
 *      stack so that the CPU uses the right stack on the next ring-0 entry.
 *
 * Reference: Intel SDM Vol. 3A §7.2.1 "TSS Descriptor".
 * ========================================================================= */

#include "tss.h"
#include "descriptor.h"
#include "log.h"

/* -------------------------------------------------------------------------
 * The single TSS instance (BSS – zero-initialised at boot).
 * The address of this variable is what we write into the GDT descriptor.
 * ------------------------------------------------------------------------- */
static tss_entry_t tss;

void tss_init(void)
{
    /* Clear the TSS (loader already zeroed .bss, but be explicit). */
    unsigned char *p = (unsigned char *)&tss;
    unsigned int   n = sizeof(tss);
    while (n--) { *p++ = 0; }

    /*
     * ss0 – the stack-segment selector the CPU loads when switching from
     * ring 3 to ring 0.  This is always the kernel data selector.
     */
    tss.ss0 = GDT_KERNEL_DATA_SELECTOR;

    /*
     * esp0 – the stack pointer the CPU uses on ring-0 entry.
     * We set a placeholder here; tss_set_kernel_stack() updates it
     * to the correct per-process value on every context switch.
     * Use the kernel's own initial stack top as a safe default.
     */
    tss.esp0 = 0;  /* Scheduler will set this before first user task runs */

    /*
     * iomap_base – offset beyond the TSS struct means no I/O map is present.
     * Leaving it 0 (which is < sizeof(tss)) would allow no I/O for ring-3;
     * set it to sizeof(tss_entry_t) to indicate "no IOPM" per Intel SDM.
     */
    tss.iomap_base = (unsigned short)sizeof(tss_entry_t);

    /*
     * Write the TSS descriptor into GDT slot TSS_GDT_INDEX.
     * gdt_set_tss_entry() is a wrapper in gdt.c that calls gdt_set_entry()
     * with the correct parameters for a 32-bit available TSS (type 0x89).
     */
    gdt_set_tss_entry(TSS_GDT_INDEX, (unsigned int)&tss,
                      sizeof(tss_entry_t) - 1);

    /*
     * Load the task register (TR) with the TSS selector.
     * ltr does NOT cause a task switch – it just initialises TR so the CPU
     * knows where to find the TSS on the next privilege-level transition.
     */
    __asm__ volatile("ltr %0" :: "r"((unsigned short)TSS_SELECTOR));

    log_info("[tss] TSS installed at 0x%x  selector=0x%x",
             (unsigned int)&tss, TSS_SELECTOR);
}

void tss_set_kernel_stack(unsigned int esp0)
{
    tss.esp0 = esp0;
}
