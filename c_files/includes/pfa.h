#ifndef PFA_H
#define PFA_H

/* =========================================================================
 * pfa.h – Physical (Page) Frame Allocator
 *
 * The PFA manages physical RAM at the granularity of 4 KB page frames.
 * It uses a compact flat bitmap: one bit per frame, 0 = free, 1 = used.
 *
 * Memory requirements for the bitmap itself:
 *   4 GB of RAM = 1,048,576 frames → 32 KB of bitmap storage.
 *   The bitmap lives in the kernel's .bss section (statically allocated)
 *   so it is automatically marked as "used" when the PFA is initialised.
 *
 * Typical call sequence during boot:
 *   1. pfa_init()  – parse the Multiboot mmap and mark kernel frames used.
 *   2. pfa_alloc_frame() – allocate one physical 4 KB frame.
 *   3. pfa_free_frame()  – release a frame back to the pool.
 *
 * Reference: Section 10 of the course notes; OSDev wiki "Page Frame
 *            Allocator".
 * ========================================================================= */

#include "multiboot.h"

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

/** FRAME_SIZE – smallest unit managed by the PFA (4 KB). */
#define FRAME_SIZE        0x1000u          /* 4 096 bytes                    */

/**
 * PFA_MAX_FRAMES – maximum number of 4 KB frames the bitmap can track.
 * 4 GB / 4 KB = 1 048 576 frames.
 */
#define PFA_MAX_FRAMES    1048576u

/**
 * PFA_BITMAP_WORDS – number of 32-bit words in the bitmap array.
 * Each word tracks 32 frames (one bit per frame).
 */
#define PFA_BITMAP_WORDS  (PFA_MAX_FRAMES / 32u)   /* 32 768 words = 128 KB */

/* -------------------------------------------------------------------------
 * Sentinel value returned by pfa_alloc_frame() on failure.
 * ------------------------------------------------------------------------- */
#define PFA_ALLOC_FAIL    0xFFFFFFFFu

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * pfa_init – Initialise the allocator from the Multiboot memory map.
 *
 * Algorithm:
 *   1. Mark every frame in the bitmap as "used" (safe default).
 *   2. Walk the Multiboot mmap.  For every region of type
 *      MULTIBOOT_MEMORY_AVAILABLE, free all 4 KB frames within it.
 *   3. Re-mark as "used":
 *      a) The first 1 MB (BIOS/I-O area; frames 0x00000000–0x000FFFFF).
 *      b) The kernel image itself  [phys_kernel_start, phys_kernel_end).
 *
 * @param mb               Pointer to the Multiboot information structure
 *                         (the virtual address forwarded by loader.s).
 * @param phys_kernel_start Physical byte address of the start of the kernel.
 * @param phys_kernel_end   Physical byte address one past the end of the
 *                          kernel (i.e. the first byte not used by the image).
 */
void pfa_init(multiboot_info_t *mb,
              unsigned int phys_kernel_start,
              unsigned int phys_kernel_end);

/**
 * pfa_alloc_frame – Allocate one free 4 KB physical frame.
 *
 * Scans the bitmap for the first free frame, marks it "used", and returns
 * its physical byte address.  Returns PFA_ALLOC_FAIL if the system is out
 * of physical memory.
 *
 * @return  Physical address of the allocated frame, or PFA_ALLOC_FAIL.
 */
unsigned int pfa_alloc_frame(void);

/**
 * pfa_free_frame – Return a 4 KB physical frame to the free pool.
 *
 * @param phys_addr  Physical byte address of the frame to release.
 *                   Must be 4 KB-aligned.
 */
void pfa_free_frame(unsigned int phys_addr);

/**
 * pfa_free_count – Return the number of currently free frames.
 *
 * Useful for diagnostics / logging.
 *
 * @return  Count of free (bit = 0) entries in the bitmap.
 */
unsigned int pfa_free_count(void);

/* =========================================================================
 * Low-level bitmap helpers (exposed for testing / paging.c use)
 * ========================================================================= */

/**
 * pfa_set_frame – Mark a single 4 KB frame as "used" in the bitmap.
 *
 * @param phys_addr  Physical byte address of the frame (4 KB-aligned).
 */
void pfa_set_frame(unsigned int phys_addr);

/**
 * pfa_clear_frame – Mark a single 4 KB frame as "free" in the bitmap.
 *
 * @param phys_addr  Physical byte address of the frame (4 KB-aligned).
 */
void pfa_clear_frame(unsigned int phys_addr);

/**
 * pfa_test_frame – Query the state of a frame.
 *
 * @param phys_addr  Physical byte address of the frame (4 KB-aligned).
 * @return  1 if the frame is used, 0 if free.
 */
int pfa_test_frame(unsigned int phys_addr);

#endif /* PFA_H */
