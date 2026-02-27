#ifndef PFA_H
#define PFA_H

/* =========================================================================
 * pfa.h – Physical (Page) Frame Allocator  (Buddy System)
 *
 * Replaces the original flat-bitmap allocator with a power-of-2 Buddy
 * System, as used by Linux and described in §10 of the course notes.
 *
 * How the Buddy System works
 * --------------------------
 * Physical RAM is managed as a hierarchy of block sizes, each a power of 2
 * multiples of FRAME_SIZE (4 KB):
 *
 *   Order 0 →     1 frame  =   4 KB
 *   Order 1 →     2 frames =   8 KB
 *   Order 2 →     4 frames =  16 KB
 *   ...
 *   Order 18 → 256 K frames =   1 GB
 *
 * A per-order doubly-linked free list tracks blocks at each level.
 * A block and its "buddy" (the neighbouring block of the same size) can be
 * merged into a block one order higher when both are free.
 *
 * Buddy address formula (XOR trick)
 * ----------------------------------
 *   Given a block starting at frame index f at order o:
 *       buddy_index = f  XOR  (1 << o)
 *
 *   Example: frame 0 at order 3 -> buddy = 0 ^ 8 = 8  (covers frames 8-15)
 *            frame 8 at order 3 -> buddy = 8 ^ 8 = 0  (symmetric)
 *
 * Allocation (split down)
 * ------------------------
 *   1. Scan free_lists[0..MAX_ORDER] for the first non-empty list at order k.
 *   2. Pop the block.
 *   3. While k > 0: split - place the upper half in free_lists[k-1], k--.
 *   4. Return the 4 KB frame at order 0.
 *
 * Deallocation (merge up)
 * ------------------------
 *   1. Start at order 0 with freed frame index f.
 *   2. buddy = f ^ (1 << order).
 *   3. If buddy is free AND at same order: remove it, f = min(f,buddy),
 *      order++, go to 2.
 *   4. Push f into free_lists[order].
 *
 * BSS footprint
 * -------------
 *   Each tracked frame has a 12-byte frame_desc_t.
 *   BUDDY_MAX_FRAMES = 32768 -> covers 128 MB of physical RAM.
 *   BSS cost = 32768 x 12 = 384 KB.
 *
 * References
 * ----------
 *   Course notes section 10 "Page Frame Allocation / Buddy System"
 *   OSDev wiki "Buddy memory allocation"
 *   Linux kernel mm/page_alloc.c (conceptual reference)
 * ========================================================================= */

#include "multiboot.h"

/* -------------------------------------------------------------------------
 * Sizes and limits
 * ------------------------------------------------------------------------- */

/** FRAME_SIZE - one 4 KB page frame (smallest allocatable unit, order 0). */
#define FRAME_SIZE          0x1000u

/**
 * BUDDY_MAX_ORDER - highest supported block order.
 * A block at order 18 spans 2^18 * 4 KB = 1 GB of contiguous RAM.
 */
#define BUDDY_MAX_ORDER     18u

/**
 * BUDDY_MAX_FRAMES - maximum number of 4 KB frames tracked.
 * 32768 * 4 KB = 128 MB.  Increase to 65536 for 256 MB, etc.
 * Each additional frame costs 12 bytes of .bss.
 */
#define BUDDY_MAX_FRAMES    32768u

/** BUDDY_NONE - sentinel for "no next/prev node" in a free list. */
#define BUDDY_NONE          0xFFFFFFFFu

/** PFA_ALLOC_FAIL - returned by pfa_alloc_frame() when out of memory. */
#define PFA_ALLOC_FAIL      0xFFFFFFFFu

/* =========================================================================
 * Per-frame descriptor
 *
 * Only the HEAD frame of a free buddy block has meaningful is_free, order,
 * bl_next and bl_prev values.  Interior frames carry is_free = 0.
 *
 *   sizeof(frame_desc_t) = 12 bytes
 *   Total .bss for 32768 frames = 32768 * 12 = 384 KB
 * ========================================================================= */
typedef struct {
    unsigned char  is_free;  /* 1 = head of a free buddy block at 'order'    */
    unsigned char  order;    /* order of the block (valid only when is_free)  */
    unsigned char  present;  /* 1 = frame is backed by real RAM in mmap       */
    unsigned char  _pad;     /* alignment padding                              */
    unsigned int   bl_next;  /* frame index of next free block (BUDDY_NONE)  */
    unsigned int   bl_prev;  /* frame index of prev free block (BUDDY_NONE)  */
} frame_desc_t;

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * pfa_init - Initialise the Buddy allocator from the Multiboot memory map.
 *
 * Steps:
 *   1. Zero all frame_desc_t entries; set every free list head to BUDDY_NONE.
 *   2. Walk the Multiboot mmap.  For each AVAILABLE region, mark each 4 KB
 *      frame as 'present' then call pfa_free_frame() - which inserts it at
 *      order 0 and immediately merges upward with its buddy if possible.
 *   3. Frames in the first 1 MB (BIOS/IO) and the kernel image range
 *      [phys_kernel_start, phys_kernel_end) are skipped (never freed).
 *
 * @param mb                Virtual pointer to the Multiboot info structure.
 * @param phys_kernel_start Physical byte address of kernel image start.
 * @param phys_kernel_end   Physical byte address one past the kernel end.
 */
void pfa_init(multiboot_info_t *mb,
              unsigned int phys_kernel_start,
              unsigned int phys_kernel_end);

/**
 * pfa_alloc_frame - Allocate one free 4 KB physical frame (order 0).
 *
 * Scans free_lists[0..BUDDY_MAX_ORDER] for the first non-empty list at
 * order k, pops the block, and splits it down to order 0, re-inserting
 * each discarded upper half into free_lists[k-1].
 *
 * @return  Physical byte address of the allocated frame, or PFA_ALLOC_FAIL.
 */
unsigned int pfa_alloc_frame(void);

/**
 * pfa_free_frame - Return a 4 KB physical frame to the buddy pool.
 *
 * Inserts at order 0 and repeatedly merges with its buddy while the buddy
 * is free at the same order, walking up toward BUDDY_MAX_ORDER.
 *
 * @param phys_addr  4 KB-aligned physical byte address to release.
 */
void pfa_free_frame(unsigned int phys_addr);

/**
 * pfa_free_count - Count all currently free 4 KB frames.
 *
 * Walks every free list and accumulates (2^order) frames per block.
 * O(free_block_count) - use only for diagnostics.
 *
 * @return  Total number of free 4 KB page frames across all orders.
 */
unsigned int pfa_free_count(void);

/* =========================================================================
 * Frame-level helpers (backward-compatible with old bitmap API)
 * ========================================================================= */

/**
 * pfa_set_frame - Reserve a specific 4 KB frame (mark it as used).
 * If the frame is free at order 0 it is unlinked from its free list.
 */
void pfa_set_frame(unsigned int phys_addr);

/**
 * pfa_clear_frame - Release a specific 4 KB frame (alias: pfa_free_frame).
 */
void pfa_clear_frame(unsigned int phys_addr);

/**
 * pfa_test_frame - Check whether a frame is free.
 * @return  0 if free, 1 if used or absent.
 */
int pfa_test_frame(unsigned int phys_addr);

#endif /* PFA_H */
