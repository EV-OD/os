#ifndef PAGING_H
#define PAGING_H

/* =========================================================================
 * paging.h – x86 paging definitions and API
 *
 * The kernel currently uses identity paging with 4 MB pages set up at
 * compile / boot time in loader.s (section 9.2.1).  This header exposes
 * the page-directory entry flag bits, the page_directory symbol that lives
 * in assembly, and a small set of helper functions for future use.
 *
 * Reference: Intel Software Developer's Manual Vol. 3A, Chapter 4.
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * Page-directory / page-table entry flag bits (Intel Vol. 3A Ch 4.3)
 * ------------------------------------------------------------------------- */

#define PDE_PRESENT      (1u << 0)   /* P   – entry is present               */
#define PDE_WRITABLE     (1u << 1)   /* R/W – read-write  (0 = read-only)     */
#define PDE_USER         (1u << 2)   /* U/S – PL3 accessible (0 = PL0 only)  */
#define PDE_WRITE_THRU   (1u << 3)   /* PWT – write-through caching           */
#define PDE_CACHE_DIS    (1u << 4)   /* PCD – cache disabled                  */
#define PDE_ACCESSED     (1u << 5)   /* A   – set by CPU on any access        */
#define PDE_DIRTY        (1u << 6)   /* D   – set by CPU on write (4 MB PDEs) */
#define PDE_PAGE_SIZE    (1u << 7)   /* PS  – 1 = 4 MB page, 0 = PT pointer  */
#define PDE_GLOBAL       (1u << 8)   /* G   – global page (requires CR4.PGE) */

/* Convenience combination for a present, writable, 4 MB identity PDE */
#define PDE_4MB_RW  (PDE_PRESENT | PDE_WRITABLE | PDE_PAGE_SIZE)

/* -------------------------------------------------------------------------
 * Page / frame sizes
 * ------------------------------------------------------------------------- */

#define PAGE_SIZE_4KB   0x1000u     /*   4 KB */
#define PAGE_SIZE_4MB   0x400000u   /*   4 MB */

/* Number of entries in a page directory or page table */
#define PAGE_DIR_ENTRIES    1024u
#define PAGE_TABLE_ENTRIES  1024u

/* -------------------------------------------------------------------------
 * Types
 * ------------------------------------------------------------------------- */

/*
 * pde_t – a single 32-bit page-directory entry.
 *
 * For a 4 MB PDE (PDE_PAGE_SIZE set):
 *   bits 31-22 : physical frame address (upper 10 bits of a 4 MB-aligned addr)
 *   bits  8- 0 : flag bits (see PDE_* above)
 *
 * For a 4 KB PDE (PDE_PAGE_SIZE clear):
 *   bits 31-12 : physical address of a 4 KB-aligned page table
 *   bits 11- 0 : flag bits
 */
typedef unsigned int pde_t;

/* pte_t – a single 32-bit page-table entry (4 KB pages) */
typedef unsigned int pte_t;

/* -------------------------------------------------------------------------
 * Symbols exported from loader.s
 * ------------------------------------------------------------------------- */

/*
 * The identity-mapped page directory created at compile time in loader.s.
 * Entry i maps virtual 4 MB region [i*4MB, (i+1)*4MB) to the same physical
 * range.  All 1024 entries are populated, covering the full 4 GB space.
 */
extern pde_t page_directory[PAGE_DIR_ENTRIES];

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/**
 * paging_init – verify and log the paging state established by loader.s.
 *
 * Called from kernel_init() after GDT, IDT and serial are ready so that
 * the log output is visible.  In future phases this function will rebuild
 * the page directory for a higher-half kernel layout.
 */
void paging_init(void);

/**
 * paging_cr3 – return the physical address currently stored in CR3.
 */
unsigned int paging_cr3(void);

/**
 * paging_invlpg – invalidate the TLB entry for a single virtual address.
 *
 * Must be called after modifying a PDE or PTE that was previously marked
 * present.  If the entry was not-present before the change, invlpg is
 * unnecessary (but harmless).
 *
 * @param vaddr  Any virtual address within the 4 KB / 4 MB page to flush.
 */
void paging_invlpg(void *vaddr);

/**
 * paging_map_4mb – install a single 4 MB identity PDE.
 *
 * @param index  Page-directory index (0-1023).  Virtual address covered is
 *               [index * 4MB, (index+1) * 4MB).
 * @param flags  Additional flag bits (OR-ed with PDE_4MB_RW internally).
 *               Pass 0 for defaults (present, writable, supervisor only).
 */
void paging_map_4mb(unsigned int index, unsigned int flags);

#endif /* PAGING_H */
