/* =========================================================================
 * pfa.c – Physical (Page) Frame Allocator  (Buddy System)
 *
 * Algorithm summary
 * -----------------
 * RAM is managed as a set of power-of-2-sized blocks.  A free list is kept
 * for each order 0..BUDDY_MAX_ORDER.  On allocation the smallest available
 * block is split down to order 0;  on free the released frame is merged
 * upward with its XOR-buddy as long as the buddy is also free.
 *
 * Free list representation
 * ------------------------
 * Each free LIST NODE is the first frame of the block itself – no separate
 * allocation needed.  The next/prev indices inside frame_desc_t link the
 * nodes.  The static frames[] array (in .bss) is the only metadata store.
 *
 * Internal fragmentation note
 * ----------------------------
 * The Buddy System suffers from internal fragmentation: a request for 5 KB
 * is satisfied with an 8 KB block (order 1), wasting 3 KB.  For small kernel
 * objects, the kheap malloc() layer (which slices buddy blocks) mitigates
 * this.
 *
 * References
 * ----------
 *   Course notes section 10, OSDev wiki "Buddy memory allocation",
 *   Linux mm/page_alloc.c (conceptual reference only)
 * ========================================================================= */

#include "pfa.h"
#include "paging.h"
#include "log.h"

/* =========================================================================
 * Static data  (.bss – zero-initialised by loader.s)
 * ========================================================================= */

/*
 * frames[] – per-frame descriptor array.
 * 32768 entries * 12 bytes = 384 KB of .bss.
 */
static frame_desc_t frames[BUDDY_MAX_FRAMES];

/*
 * free_lists[] – head frame-index for each order's free list.
 * Initialised to BUDDY_NONE in pfa_init().
 */
static unsigned int free_lists[BUDDY_MAX_ORDER + 1u];

/* =========================================================================
 * Free-list helpers  (O(1) push / pop / remove)
 * ========================================================================= */

/*
 * list_push – prepend frame_idx to free_lists[order].
 *
 * Sets is_free and order fields of the frame descriptor.
 */
static void list_push(unsigned int order, unsigned int frame_idx)
{
    unsigned int old_head = free_lists[order];

    frames[frame_idx].is_free = 1;
    frames[frame_idx].order   = (unsigned char)order;
    frames[frame_idx].bl_next = old_head;
    frames[frame_idx].bl_prev = BUDDY_NONE;

    if (old_head != BUDDY_NONE) {
        frames[old_head].bl_prev = frame_idx;
    }

    free_lists[order] = frame_idx;
}

/*
 * list_pop – remove and return the head of free_lists[order].
 * Returns BUDDY_NONE if the list is empty.
 */
static unsigned int list_pop(unsigned int order)
{
    unsigned int idx = free_lists[order];

    if (idx == BUDDY_NONE) {
        return BUDDY_NONE;
    }

    free_lists[order] = frames[idx].bl_next;

    if (frames[idx].bl_next != BUDDY_NONE) {
        frames[frames[idx].bl_next].bl_prev = BUDDY_NONE;
    }

    frames[idx].is_free = 0;
    return idx;
}

/*
 * list_remove – unlink frame_idx from free_lists[order].
 * frame_idx must currently be in that list.
 */
static void list_remove(unsigned int order, unsigned int frame_idx)
{
    unsigned int prev = frames[frame_idx].bl_prev;
    unsigned int next = frames[frame_idx].bl_next;

    if (prev != BUDDY_NONE) {
        frames[prev].bl_next = next;
    } else {
        /* frame_idx was the head */
        free_lists[order] = next;
    }

    if (next != BUDDY_NONE) {
        frames[next].bl_prev = prev;
    }

    frames[frame_idx].is_free = 0;
    frames[frame_idx].bl_next = BUDDY_NONE;
    frames[frame_idx].bl_prev = BUDDY_NONE;
}

/* =========================================================================
 * Buddy address calculation
 *
 * For a block whose head is at frame index f at order o:
 *   buddy_index = f XOR (1 << o)
 *
 * Proof: the two buddies together form a (o+1)-order-aligned block.  The
 * lower-addressed block has bit o clear; the upper has it set.  XOR flips
 * exactly that bit, mapping each block to its partner.
 * ========================================================================= */
static inline unsigned int buddy_of(unsigned int frame_idx, unsigned int order)
{
    return frame_idx ^ (1u << order);
}

/* =========================================================================
 * pfa_init
 * ========================================================================= */
void pfa_init(multiboot_info_t *mb,
              unsigned int phys_kernel_start,
              unsigned int phys_kernel_end)
{
    multiboot_memory_map_t *mmap;
    unsigned int mmap_end;
    unsigned int o, addr, idx;
    unsigned int total_kb = 0;

    log_info("[pfa] buddy init: kernel phys [0x%x, 0x%x)",
             phys_kernel_start, phys_kernel_end);

    /* ------------------------------------------------------------------
     * Step 1: Initialise all free lists to empty and zero frame descs.
     *         frames[] is in .bss so already zero, but be explicit.
     * ------------------------------------------------------------------ */
    for (o = 0; o <= BUDDY_MAX_ORDER; o++) {
        free_lists[o] = BUDDY_NONE;
    }
    /* frames[] already zeroed by the loader (.bss); no loop needed. */

    /* ------------------------------------------------------------------
     * Step 2: Walk Multiboot mmap.  For every AVAILABLE region, mark
     *         frames as 'present' and free them (buddy merges on the fly).
     *
     *  Skip: first 1 MB (BIOS, IVT, VGA VRAM, GRUB data).
     *  Skip: the kernel image [phys_kernel_start, phys_kernel_end).
     *  Skip: any frame index >= BUDDY_MAX_FRAMES.
     * ------------------------------------------------------------------ */
    if (!(mb->flags & MULTIBOOT_INFO_MEM_MAP)) {
        log_warning("[pfa] Multiboot mmap absent - PFA not initialised");
        return;
    }

    /*
     * mb->mmap_addr is a physical address.  The identity map (PDE[0]) was
     * removed by loader.s, so add KERNEL_VIRTUAL_BASE before dereferencing.
     */
    mmap     = (multiboot_memory_map_t *)
               ((unsigned int)mb->mmap_addr + KERNEL_VIRTUAL_BASE);
    mmap_end = (unsigned int)mb->mmap_addr + KERNEL_VIRTUAL_BASE
               + mb->mmap_length;

    log_info("[pfa] mmap at virt 0x%x  len %d bytes",
             (unsigned int)mmap, (int)mb->mmap_length);

    while ((unsigned int)mmap < mmap_end) {

        if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {

            unsigned int region_start = (unsigned int)(mmap->addr & 0xFFFFFFFFu);
            unsigned int region_end   = region_start
                                      + (unsigned int)(mmap->len  & 0xFFFFFFFFu);

            total_kb += (unsigned int)((mmap->len & 0xFFFFFFFFu) >> 10);

            /* Align region start up to next 4 KB boundary. */
            for (addr  = (region_start + FRAME_SIZE - 1u) & ~(FRAME_SIZE - 1u);
                 addr  < region_end;
                 addr += FRAME_SIZE) {

                /* Skip first 1 MB (BIOS / IVT / VGA / GRUB). */
                if (addr < 0x100000u) {
                    continue;
                }

                /* Skip the kernel image. */
                if (addr >= phys_kernel_start && addr < phys_kernel_end) {
                    continue;
                }

                idx = addr / FRAME_SIZE;

                /* Skip frames beyond our tracking capacity. */
                if (idx >= BUDDY_MAX_FRAMES) {
                    continue;
                }

                /*
                 * Mark as present so the merge logic can confirm that a
                 * buddy is a real RAM frame before attempting to coalesce.
                 */
                frames[idx].present = 1;

                /*
                 * pfa_free_frame() inserts at order 0 and merges upward
                 * with any adjacent free buddy blocks.  Calling it for
                 * each individual frame performs the "bottom-up coalescing"
                 * pass that builds the initial buddy free lists efficiently.
                 */
                pfa_free_frame(addr);
            }
        }

        /* Advance: mmap->size does NOT include itself. */
        mmap = (multiboot_memory_map_t *)
               ((unsigned int)mmap + mmap->size + sizeof(mmap->size));
    }

    log_info("[pfa] total available RAM : ~%d KB", (int)total_kb);

    /* Log free list occupancy per order for debugging. */
    {
        unsigned int order;
        for (order = 0; order <= BUDDY_MAX_ORDER; order++) {
            if (free_lists[order] != BUDDY_NONE) {
                unsigned int cnt = 0;
                unsigned int cur = free_lists[order];
                while (cur != BUDDY_NONE) {
                    cnt++;
                    cur = frames[cur].bl_next;
                }
                log_info("[pfa]   order %d : %d block(s) x %d KB = %d KB free",
                         (int)order,
                         (int)cnt,
                         (int)((FRAME_SIZE << order) / 1024u),
                         (int)(cnt * (FRAME_SIZE << order) / 1024u));
            }
        }
    }

    log_info("[pfa] total free frames: %d  (%d KB)",
             (int)pfa_free_count(),
             (int)(pfa_free_count() * (FRAME_SIZE / 1024u)));
}

/* =========================================================================
 * pfa_alloc_frame – allocate one 4 KB frame.
 * ========================================================================= */
unsigned int pfa_alloc_frame(void)
{
    unsigned int order, frame_idx, buddy_idx;

    /* Find the lowest order with a free block. */
    for (order = 0; order <= BUDDY_MAX_ORDER; order++) {
        if (free_lists[order] != BUDDY_NONE) {
            break;
        }
    }

    if (order > BUDDY_MAX_ORDER) {
        log_error("[pfa] pfa_alloc_frame: OUT OF MEMORY");
        return PFA_ALLOC_FAIL;
    }

    /* Pop the block at the found order. */
    frame_idx = list_pop(order);

    /*
     * Split down to order 0.
     * Each iteration cuts the block in half:
     *   lower half → frame_idx  (we keep working on this)
     *   upper half → buddy_idx  (pushed to free_lists[order-1])
     */
    while (order > 0u) {
        order--;
        /*
         * The upper half starts exactly 2^order frames above frame_idx.
         * buddy_of() gives the same result: frame_idx ^ (1<<order).
         * Since frame_idx is the lower half here, the buddy is always
         * the upper half (frame_idx + 2^order).
         */
        buddy_idx = frame_idx + (1u << order);

        if (buddy_idx < BUDDY_MAX_FRAMES && frames[buddy_idx].present) {
            list_push(order, buddy_idx);
        }
    }

    return frame_idx * FRAME_SIZE;
}

/* =========================================================================
 * pfa_free_frame – return a 4 KB frame to the buddy pool.
 * ========================================================================= */
void pfa_free_frame(unsigned int phys_addr)
{
    unsigned int frame_idx = phys_addr / FRAME_SIZE;
    unsigned int order     = 0;
    unsigned int buddy_idx;

    if (frame_idx >= BUDDY_MAX_FRAMES) {
        log_warning("[pfa] pfa_free_frame: 0x%x out of range", phys_addr);
        return;
    }

    /*
     * Merge loop: at each order, compute the buddy and check whether it
     * is free AND at the same order.  If yes, remove the buddy from its
     * free list, pick the lower-addressed frame as the new block head, and
     * move one order up.  Stop when merging is no longer possible.
     */
    while (order < BUDDY_MAX_ORDER) {
        buddy_idx = buddy_of(frame_idx, order);

        /* Stop if buddy is outside tracked range. */
        if (buddy_idx >= BUDDY_MAX_FRAMES) {
            break;
        }

        /* Stop if buddy is not real RAM. */
        if (!frames[buddy_idx].present) {
            break;
        }

        /* Stop if buddy is not free or is part of a different-order block. */
        if (!frames[buddy_idx].is_free || frames[buddy_idx].order != order) {
            break;
        }

        /* Remove the buddy from its current free list. */
        list_remove(order, buddy_idx);

        /* The merged block's head is the lower-addressed of the two. */
        if (buddy_idx < frame_idx) {
            frame_idx = buddy_idx;
        }

        order++;
    }

    /* Insert the (possibly merged) block at its final order. */
    list_push(order, frame_idx);
}

/* =========================================================================
 * pfa_free_count – total number of free 4 KB frames.
 *
 * Walks every free list and sums 2^order frames per block.
 * ========================================================================= */
unsigned int pfa_free_count(void)
{
    unsigned int order, count = 0;
    unsigned int cur;

    for (order = 0; order <= BUDDY_MAX_ORDER; order++) {
        cur = free_lists[order];
        while (cur != BUDDY_NONE) {
            count += (1u << order);
            cur = frames[cur].bl_next;
        }
    }
    return count;
}

/* =========================================================================
 * pfa_set_frame – forcibly mark a frame as used.
 *
 * If the frame is the head of a free order-0 block, it is unlinked.
 * Frame descriptors for other orders are not affected (use pfa_init to
 * avoid calling this on frames that are part of merged higher-order blocks).
 * ========================================================================= */
void pfa_set_frame(unsigned int phys_addr)
{
    unsigned int idx = phys_addr / FRAME_SIZE;

    if (idx >= BUDDY_MAX_FRAMES) {
        return;
    }
    if (frames[idx].is_free && frames[idx].order == 0) {
        list_remove(0, idx);
    }
}

/* =========================================================================
 * pfa_clear_frame – alias for pfa_free_frame.
 * ========================================================================= */
void pfa_clear_frame(unsigned int phys_addr)
{
    pfa_free_frame(phys_addr);
}

/* =========================================================================
 * pfa_test_frame – check whether a specific frame is free.
 * ========================================================================= */
int pfa_test_frame(unsigned int phys_addr)
{
    unsigned int idx = phys_addr / FRAME_SIZE;

    if (idx >= BUDDY_MAX_FRAMES) {
        return 1;   /* out of range = treat as used */
    }
    return frames[idx].is_free ? 0 : 1;
}
