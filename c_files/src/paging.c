/* =========================================================================
 * paging.c – x86 paging initialisation and helpers (higher-half kernel)
 *
 * The initial page directory is built at compile time in loader.s:
 *   - Entry 0   identity-maps [0, 4 MB) (removed before reaching C code).
 *   - Entry 768 maps virtual [0xC0000000, 0xC0400000) → physical [0, 4 MB).
 *
 * This module provides run-time helpers for querying and modifying the
 * paging state.
 *
 * Reference: Intel SDM Vol. 3A, Chapter 4 (Paging).
 * ========================================================================= */

#include "paging.h"
#include "log.h"

/* -------------------------------------------------------------------------
 * paging_init
 * ------------------------------------------------------------------------- */

void paging_init(void)
{
    unsigned int cr0, cr3, cr4;

    /* Read current control-register values set by loader.s. */
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));

    if (cr0 & 0x80000000u) {
        log_info("[paging] higher-half paging enabled  CR3=0x%x  PSE=%s",
                 cr3,
                 (cr4 & 0x00000010u) ? "on(4MB)" : "off(4KB)");
        log_info("[paging] PDE[0]=%s  PDE[%d]=0x%x",
                 (page_directory[0] & PDE_PRESENT) ? "present(BUG!)" : "cleared",
                 KERNEL_PAGE_INDEX,
                 page_directory[KERNEL_PAGE_INDEX]);
    } else {
        log_error("[paging] paging NOT enabled (CR0=0x%x) – loader.s error?",
                  cr0);
    }
}

/* -------------------------------------------------------------------------
 * paging_cr3
 * ------------------------------------------------------------------------- */

unsigned int paging_cr3(void)
{
    unsigned int v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

/* -------------------------------------------------------------------------
 * paging_invlpg
 * ------------------------------------------------------------------------- */

void paging_invlpg(void *vaddr)
{
    /*
     * The invlpg instruction invalidates the TLB entry associated with the
     * linear address in the memory operand.  The "memory" clobber prevents
     * the compiler from reordering loads/stores across this barrier.
     */
    __asm__ volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
}

/* -------------------------------------------------------------------------
 * paging_map_4mb
 * ------------------------------------------------------------------------- */

void paging_map_4mb(unsigned int index, unsigned int phys_frame, unsigned int flags)
{
    void *vaddr;

    if (index >= PAGE_DIR_ENTRIES) {
        log_error("[paging] paging_map_4mb: index %d out of range", index);
        return;
    }

    /*
     * The upper 10 bits of the PDE hold the physical frame number.
     * phys_frame is already the frame index (physical_address >> 22).
     */
    page_directory[index] = (phys_frame << 22) | PDE_4MB_RW | flags;

    /* Flush the TLB for the first page of this 4 MB region. */
    vaddr = (void *)(index * PAGE_SIZE_4MB);
    paging_invlpg(vaddr);

    log_debug("[paging] mapped 4MB PDE[%d] virt=0x%x → phys frame %d  PDE=0x%x",
              index, (unsigned int)vaddr, phys_frame, page_directory[index]);
}
