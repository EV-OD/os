/* =========================================================================
 * paging.c – x86 paging: kernel 4 MB PSE pages + user 4 KB pages
 *
 * The initial page directory is built at compile time in loader.s:
 *   - Entry 0   identity-maps [0, 4 MB) (removed before reaching C code).
 *   - Entry 768 maps virtual [0xC0000000, 0xC0400000) → physical [0, 4 MB).
 *
 * This module provides run-time helpers for querying and modifying the
 * paging state, and implements per-process page directories with 4 KB
 * page tables for user-mode processes.
 *
 * Reference: Intel SDM Vol. 3A, Chapter 4 (Paging).
 * ========================================================================= */

#include "paging.h"
#include "pfa.h"
#include "string.h"
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

    page_directory[index] = (phys_frame << 22) | PDE_4MB_RW | flags;

    vaddr = (void *)(index * PAGE_SIZE_4MB);
    paging_invlpg(vaddr);

    log_debug("[paging] mapped 4MB PDE[%d] virt=0x%x -> phys frame %d  PDE=0x%x",
              index, (unsigned int)vaddr, phys_frame, page_directory[index]);
}

/* =========================================================================
 * Per-process page directory management  (4 KB page tables)
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * paging_get_kernel_directory
 * ------------------------------------------------------------------------- */
unsigned int *paging_get_kernel_directory(void)
{
    return (unsigned int *)page_directory;
}

/* -------------------------------------------------------------------------
 * paging_create_user_directory
 *
 * Allocates a 4 KB physical frame for a new page directory.  Copies the
 * kernel-half PDE entries (indices 768-1023) so that kernel space is
 * shared across all processes.  Lower-half entries are zeroed.
 * ------------------------------------------------------------------------- */
unsigned int *paging_create_user_directory(void)
{
    unsigned int pd_phys;
    unsigned int *pd;
    unsigned int i;

    /* Allocate a 4 KB-aligned frame for the page directory. */
    pd_phys = pfa_alloc_frame();
    if (pd_phys == PFA_ALLOC_FAIL) {
        log_error("[paging] failed to allocate page directory frame");
        return (unsigned int *)0;
    }

    /* Convert to kernel-virtual so we can write to it. */
    pd = (unsigned int *)PHYS_TO_VIRT(pd_phys);

    /* Zero the entire directory first (user-space half = empty). */
    memset(pd, 0, PAGE_SIZE_4KB);

    /* Copy kernel PDE entries (indices 768-1023) from the boot directory.
     * These are 4 MB PSE entries that map kernel memory; sharing them
     * ensures all processes see the same kernel address space. */
    for (i = KERNEL_PAGE_INDEX; i < PAGE_DIR_ENTRIES; i++) {
        pd[i] = page_directory[i];
    }

    log_debug("[paging] created user PD phys=0x%x virt=0x%x",
              pd_phys, (unsigned int)pd);

    return pd;
}

/* -------------------------------------------------------------------------
 * paging_map_page
 *
 * Maps a single 4 KB physical page at a given virtual address in the
 * specified page directory.  If no page table exists for the PDE covering
 * this virtual address, one is allocated from the PFA.
 * ------------------------------------------------------------------------- */
void paging_map_page(unsigned int *pd, unsigned int virt,
                     unsigned int phys, unsigned int flags)
{
    unsigned int pd_idx = virt >> 22;
    unsigned int pt_idx = (virt >> 12) & 0x3FFu;
    unsigned int pt_phys;
    unsigned int *pt;

    /* If the PDE doesn't have a page table yet, allocate one. */
    if (!(pd[pd_idx] & PDE_PRESENT)) {
        pt_phys = pfa_alloc_frame();
        if (pt_phys == PFA_ALLOC_FAIL) {
            log_error("[paging] failed to allocate page table for virt 0x%x", virt);
            return;
        }
        pt = (unsigned int *)PHYS_TO_VIRT(pt_phys);
        memset(pt, 0, PAGE_SIZE_4KB);

        /* Install the PDE: point to the page table.
         * PDE flags must include USER if user pages will be placed inside. */
        pd[pd_idx] = pt_phys | PDE_PRESENT | PDE_WRITABLE | (flags & PDE_USER);
    } else {
        /* PDE already exists – extract the page table physical address. */
        pt_phys = pd[pd_idx] & 0xFFFFF000u;
        pt = (unsigned int *)PHYS_TO_VIRT(pt_phys);

        /* Promote PDE to user-accessible if the new PTE requests it. */
        if ((flags & PTE_USER) && !(pd[pd_idx] & PDE_USER)) {
            pd[pd_idx] |= PDE_USER;
        }
    }

    /* Set the page table entry. */
    pt[pt_idx] = (phys & 0xFFFFF000u) | flags;

    /* Flush the TLB for this specific virtual address. */
    paging_invlpg((void *)(virt & 0xFFFFF000u));

    log_trace("[paging] mapped 4KB virt=0x%x -> phys=0x%x flags=0x%x",
              virt & 0xFFFFF000u, phys & 0xFFFFF000u, flags);
}

/* -------------------------------------------------------------------------
 * paging_destroy_user_directory
 *
 * Frees all user-space page tables and their mapped page frames, then
 * frees the page directory frame itself.  Kernel PDE entries (768-1023)
 * are 4 MB PSE mappings shared by the kernel – we do NOT free those.
 * ------------------------------------------------------------------------- */
void paging_destroy_user_directory(unsigned int *pd)
{
    unsigned int i, j;
    unsigned int pt_phys;
    unsigned int *pt;

    if (!pd) return;

    /* Walk user-space PDE entries (indices 0 to 767). */
    for (i = 0; i < KERNEL_PAGE_INDEX; i++) {
        if (!(pd[i] & PDE_PRESENT)) continue;

        /* Skip 4 MB PSE entries (shouldn't be in user space, but guard). */
        if (pd[i] & PDE_PAGE_SIZE) continue;

        pt_phys = pd[i] & 0xFFFFF000u;
        pt = (unsigned int *)PHYS_TO_VIRT(pt_phys);

        /* Free every present 4 KB page frame mapped by this page table. */
        for (j = 0; j < PAGE_TABLE_ENTRIES; j++) {
            if (pt[j] & PTE_PRESENT) {
                pfa_free_frame(pt[j] & 0xFFFFF000u);
            }
        }

        /* Free the page table frame itself. */
        pfa_free_frame(pt_phys);
    }

    /* Free the page directory frame. */
    pfa_free_frame(VIRT_TO_PHYS(pd));

    log_debug("[paging] destroyed user PD at 0x%x", (unsigned int)pd);
}

/* -------------------------------------------------------------------------
 * paging_switch_directory
 *
 * Loads a page directory into CR3, causing a full TLB flush.
 * The pd parameter is a kernel-virtual pointer; we convert to physical.
 * ------------------------------------------------------------------------- */
void paging_switch_directory(unsigned int *pd)
{
    unsigned int phys = VIRT_TO_PHYS(pd);
    __asm__ volatile("mov %0, %%cr3" :: "r"(phys) : "memory");
}
