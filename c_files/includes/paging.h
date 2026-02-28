#ifndef PAGING_H
#define PAGING_H

/* =========================================================================
 * paging.h – x86 paging: 4 MB PSE pages + 4 KB page tables
 *
 * The kernel uses a higher-half layout: virtual addresses 0xC0000000+
 * map to physical 0x00000000+.  The initial 4 MB page is set up at
 * boot time in loader.s.
 *
 * User-mode processes use fine-grained 4 KB page tables so we can map
 * individual pages with user-accessible flags while keeping per-process
 * virtual address spaces isolated.
 *
 * Reference: Intel Software Developer's Manual Vol. 3A, Chapter 4.
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * Page-directory entry (PDE) flag bits (Intel Vol. 3A Ch 4.3)
 * ------------------------------------------------------------------------- */

#define PDE_PRESENT      (1u << 0)   /* P   – entry is present               */
#define PDE_WRITABLE     (1u << 1)   /* R/W – read-write  (0 = read-only)    */
#define PDE_USER         (1u << 2)   /* U/S – PL3 accessible (0 = PL0 only)  */
#define PDE_WRITE_THRU   (1u << 3)   /* PWT – write-through caching          */
#define PDE_CACHE_DIS    (1u << 4)   /* PCD – cache disabled                 */
#define PDE_ACCESSED     (1u << 5)   /* A   – set by CPU on any access       */
#define PDE_DIRTY        (1u << 6)   /* D   – set by CPU on write (4 MB PDEs)*/
#define PDE_PAGE_SIZE    (1u << 7)   /* PS  – 1 = 4 MB page, 0 = PT pointer */
#define PDE_GLOBAL       (1u << 8)   /* G   – global page (requires CR4.PGE) */

/* Convenience combination for a present, writable, 4 MB identity PDE */
#define PDE_4MB_RW  (PDE_PRESENT | PDE_WRITABLE | PDE_PAGE_SIZE)

/* -------------------------------------------------------------------------
 * Page-table entry (PTE) flag bits (4 KB pages)
 * ------------------------------------------------------------------------- */

#define PTE_PRESENT      (1u << 0)   /* P   – entry is present               */
#define PTE_RW           (1u << 1)   /* R/W – read-write                     */
#define PTE_USER         (1u << 2)   /* U/S – PL3 accessible                 */
#define PTE_WRITE_THRU   (1u << 3)   /* PWT – write-through                  */
#define PTE_CACHE_DIS    (1u << 4)   /* PCD – cache disabled                 */
#define PTE_ACCESSED     (1u << 5)   /* A   – accessed by CPU                */
#define PTE_DIRTY        (1u << 6)   /* D   – dirty (written)                */

/* -------------------------------------------------------------------------
 * Page / frame sizes
 * ------------------------------------------------------------------------- */

#define PAGE_SIZE_4KB   0x1000u     /*   4 KB */
#define PAGE_SIZE_4MB   0x400000u   /*   4 MB */

/* Number of entries in a page directory or page table */
#define PAGE_DIR_ENTRIES    1024u
#define PAGE_TABLE_ENTRIES  1024u

/* -------------------------------------------------------------------------
 * Higher-half kernel constants
 * ------------------------------------------------------------------------- */

#define KERNEL_VIRTUAL_BASE  0xC0000000u
#define KERNEL_PAGE_INDEX    (KERNEL_VIRTUAL_BASE >> 22)   /* PDE index 768 */

/* Convert between physical and virtual addresses within the kernel mapping. */
#define PHYS_TO_VIRT(p)  ((void *)((unsigned int)(p) + KERNEL_VIRTUAL_BASE))
#define VIRT_TO_PHYS(v)  ((unsigned int)(v) - KERNEL_VIRTUAL_BASE)

/* -------------------------------------------------------------------------
 * User-space virtual address layout
 *
 * 0x00000000 – 0x003FFFFF : unmapped (null-pointer guard, 4 MB)
 * 0x08048000               : default user code start (traditional ELF)
 * 0xBFFFF000               : user stack page (grows downward from top)
 * 0xC0000000 – 0xFFFFFFFF : kernel space (shared across all processes)
 * ------------------------------------------------------------------------- */
#define USER_CODE_VADDR      0x08048000u
#define USER_STACK_TOP       0xBFFFFFFCu   /* initial ESP for user process  */
#define USER_STACK_PAGE      0xBFFFF000u   /* 4KB page containing the stack */

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

extern pde_t page_directory[PAGE_DIR_ENTRIES];

/* -------------------------------------------------------------------------
 * Core API (kernel paging)
 * ------------------------------------------------------------------------- */

void paging_init(void);
unsigned int paging_cr3(void);
void paging_invlpg(void *vaddr);
void paging_map_4mb(unsigned int index, unsigned int phys_frame, unsigned int flags);

/**
 * paging_map_full_kernel_ram – extend kernel virtual memory mapping to cover
 * all physical RAM up to `total_bytes`.
 *
 * Adds 4 MB PSE entries in PDE[769..] so that PHYS_TO_VIRT() works for any
 * physical address returned by pfa_alloc_frame().  Must be called BEFORE any
 * code that uses PHYS_TO_VIRT on frames above 4 MB.
 */
void paging_map_full_kernel_ram(unsigned int total_bytes);

/* -------------------------------------------------------------------------
 * Per-process page directory management (user-mode support)
 * ------------------------------------------------------------------------- */

/**
 * paging_create_user_directory – allocate a new page directory for a user
 * process.  Copies kernel PDE entries (indices 768-1023) from the boot
 * page directory.  Lower-half entries are zeroed.
 *
 * @return  Virtual (kernel-mapped) pointer to the new 4 KB page directory,
 *          or NULL (0) on allocation failure.
 */
unsigned int *paging_create_user_directory(void);

/**
 * paging_map_page – map a single 4 KB page in a given page directory.
 *
 * If the PDE for this virtual address doesn't have a page table yet,
 * one is allocated from the PFA and installed.
 *
 * @param pd     Virtual address of the target page directory.
 * @param virt   Virtual address to map (will be 4 KB-aligned down).
 * @param phys   Physical address of the 4 KB frame to map.
 * @param flags  PTE flags (PTE_PRESENT | PTE_RW | PTE_USER …).
 */
void paging_map_page(unsigned int *pd, unsigned int virt,
                     unsigned int phys, unsigned int flags);

/**
 * paging_destroy_user_directory – free a user page directory and all its
 * non-kernel page tables.  User-space page frames mapped by PTEs are also
 * freed back to the PFA.
 *
 * @param pd  Virtual address of the page directory to destroy.
 */
void paging_destroy_user_directory(unsigned int *pd);

/**
 * paging_switch_directory – load a page directory into CR3.
 *
 * @param pd  Virtual (kernel-mapped) pointer to the page directory.
 */
void paging_switch_directory(unsigned int *pd);

/**
 * paging_get_kernel_directory – return the boot/kernel page directory.
 */
unsigned int *paging_get_kernel_directory(void);

/**
 * paging_map_mmio – identity-map a region of MMIO (device) memory into the
 * kernel's virtual address space using 4 MB PSE page-directory entries.
 *
 * Because MMIO regions (e.g. VESA framebuffers) live above 0xC0000000
 * physically (or at least outside the normal RAM area), we use the
 * corresponding PDE slot at virtual == physical so the kernel can access
 * them without an explicit offset.  The mapping uses Write-Through +
 * Cache-Disabled flags as recommended for MMIO regions.
 *
 * @param phys_base  Physical start address of the MMIO region.
 *                   Will be aligned down to the nearest 4 MB boundary.
 * @param size       Byte length of the region.  Enough 4 MB PDEs are
 *                   created to cover [phys_base, phys_base + size).
 *
 * @return           The virtual address corresponding to @p phys_base
 *                   (i.e. phys_base itself, since we identity-map).
 *                   Returns 0 on error (phys_base == 0 or conflicts with
 *                   the kernel higher-half mapping).
 */
unsigned int paging_map_mmio(unsigned int phys_base, unsigned int size);

#endif /* PAGING_H */
