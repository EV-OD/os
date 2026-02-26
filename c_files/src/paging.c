/* =========================================================================
 * paging.c – x86 paging initialisation and helpers
 *
 * The actual page directory is built at compile time in loader.s and paging
 * is enabled there (before kmain) using 4 MB identity pages.  This module
 * provides run-time helpers for querying and modifying the paging state.
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
        log_info("[paging] identity paging enabled  CR3=0x%x  PSE=%s",
                 cr3,
                 (cr4 & 0x00000010u) ? "on(4MB)" : "off(4KB)");
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

void paging_map_4mb(unsigned int index, unsigned int flags)
{
    void *vaddr;

    if (index >= PAGE_DIR_ENTRIES) {
        log_error("[paging] paging_map_4mb: index %d out of range", index);
        return;
    }

    /*
     * Physical frame address for a 4 MB identity entry: the upper 10 bits of
     * the 32-bit PDE hold the frame number, so frame i starts at i * 4 MB.
     * The lower 12 flag bits are supplied by the caller (plus our defaults).
     */
    page_directory[index] = (index << 22) | PDE_4MB_RW | flags;

    /* Flush the TLB for the first page of this 4 MB region. */
    vaddr = (void *)(index * PAGE_SIZE_4MB);
    paging_invlpg(vaddr);

    log_debug("[paging] mapped 4MB entry %d  virt=0x%x  PDE=0x%x",
              index, (unsigned int)vaddr, page_directory[index]);
}
