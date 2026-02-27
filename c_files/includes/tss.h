#ifndef TSS_H
#define TSS_H

/* =========================================================================
 * tss.h – x86 Task State Segment (TSS)
 *
 * The TSS is a data structure the CPU uses during privilege-level transitions.
 * When an interrupt/exception occurs and the CPU transitions from ring 3 → ring 0,
 * it reads esp0 and ss0 from the TSS to load the kernel stack pointer.
 *
 * We use a single, static TSS (software multi-tasking via scheduler, not
 * hardware task-switching).  The only fields we actively use are:
 *   ss0  – kernel stack segment selector (always GDT_KERNEL_DATA_SELECTOR)
 *   esp0 – kernel stack pointer for the CURRENT process (updated on switch)
 *
 * The TSS descriptor in the GDT must have DPL=0 and type=0x89 (32-bit TSS).
 * After gdt_init() the TR (task register) is loaded with `ltr` in tss_init().
 *
 * Reference: Intel SDM Vol. 3A §7.2 "TSS Descriptor".
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * Full 32-bit TSS layout (Intel SDM Vol. 3A Figure 7-2)
 * We only need ss0/esp0 at runtime; all other fields stay zero.
 * ------------------------------------------------------------------------- */
typedef struct tss_entry {
    unsigned int prev_tss;   /* Selector of the previous TSS (hardware task link)  */
    unsigned int esp0;       /* Stack pointer for ring 0 (updated each task switch) */
    unsigned int ss0;        /* Stack segment for ring 0 (= kernel data selector)   */
    unsigned int esp1;       /* Ring 1 stack pointer (unused)                       */
    unsigned int ss1;        /* Ring 1 stack segment (unused)                       */
    unsigned int esp2;       /* Ring 2 stack pointer (unused)                       */
    unsigned int ss2;        /* Ring 2 stack segment (unused)                       */
    unsigned int cr3;        /* Page directory base (unused; we set CR3 manually)   */
    unsigned int eip;        /* Instruction pointer (hardware task switch only)     */
    unsigned int eflags;     /* Flags (hardware task switch only)                   */
    unsigned int eax;        /* General registers (hardware task switch only)       */
    unsigned int ecx;
    unsigned int edx;
    unsigned int ebx;
    unsigned int esp;
    unsigned int ebp;
    unsigned int esi;
    unsigned int edi;
    unsigned int es;         /* Segment registers (hardware task switch only)       */
    unsigned int cs;
    unsigned int ss;
    unsigned int ds;
    unsigned int fs;
    unsigned int gs;
    unsigned int ldt;        /* LDT selector (unused)                               */
    unsigned short trap;     /* Debug trap flag (unused)                            */
    unsigned short iomap_base; /* I/O permission bitmap offset (unused)             */
} __attribute__((packed)) tss_entry_t;

/* -------------------------------------------------------------------------
 * GDT index for the TSS descriptor.
 * Index 5 → selector = 5 * 8 = 0x28.
 * (Index 3 = user code, Index 4 = user data → see descriptor.h)
 * ------------------------------------------------------------------------- */
#define TSS_GDT_INDEX    5
#define TSS_SELECTOR     (TSS_GDT_INDEX * 8)   /* 0x28 */

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/**
 * tss_init – install the TSS descriptor in the GDT and load TR (task register).
 *
 * Must be called AFTER gdt_init() because it writes into the GDT table via
 * the gdt_set_tss_entry() helper exported from gdt.c.
 */
void tss_init(void);

/**
 * tss_set_kernel_stack – update esp0 in the TSS.
 *
 * Called on every process switch so that when the NEW process is interrupted
 * (ring 3 → ring 0, or double-fault), the CPU loads the correct kernel stack.
 *
 * @param esp0  Virtual address of the TOP of the new process's kernel stack.
 */
void tss_set_kernel_stack(unsigned int esp0);

#endif /* TSS_H */
