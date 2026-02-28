#ifndef KHEAP_H
#define KHEAP_H

/* =========================================================================
 * kheap.h – Kernel Heap Allocator (kmalloc / kfree)
 *
 * Implements a simple doubly-linked-list heap with:
 *   • First-fit block search.
 *   • Block splitting  – avoids wasting memory when the found block is
 *     larger than requested.
 *   • Block coalescing – merges free neighbours on every kfree() call to
 *     prevent fragmentation.
 *   • Magic-number guard – each block header carries 0xDEADBEEF; kfree()
 *     checks this before touching the block and panics on corruption.
 *
 * Heap layout (inside the initial 4 MB kernel mapping)
 * -----------------------------------------------------
 *   The heap occupies the virtual range [KHEAP_VSTART, KHEAP_VEND).
 *   Both addresses lie within 0xC0000000–0xC03FFFFF, so they are backed
 *   by the 4 MB PSE page that loader.s creates – no extra page tables are
 *   needed for the initial heap.
 *
 *   kheap_init() places a single "free" block header at KHEAP_VSTART that
 *   spans the entire region.  Subsequent kmalloc() / kfree() calls carve
 *   or merge blocks within that region.
 *
 * Block memory layout (in bytes)
 * --------------------------------
 *   [ block_header_t (20 B) | <user data: header.size bytes> ]
 *
 *   header.size  = size of the DATA area only (NOT including the header).
 *   hptr         = pointer to the block_header_t.
 *   ptr (user)   = hptr + sizeof(block_header_t).
 *
 * References
 * ----------
 *   • Course notes §10.3 "A Kernel Heap"
 *   • Kernighan & Ritchie – "The C Programming Language", §8.7
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * Heap virtual address range
 *
 * Placed well within the first 4 MB kernel PSE mapping so no extra page
 * tables are required for the initial implementation.  The heap starts
 * at 0xC0300000 and extends to 0xC0400000 (1 MB of initial heap space).
 * ------------------------------------------------------------------------- */
#define KHEAP_VSTART  0xC0300000u   /* virtual start of heap                  */
#define KHEAP_VEND    0xC0800000u   /* virtual end  of heap (5 MB – fits 3 MB FB back-buffer) */
#define KHEAP_SIZE    (KHEAP_VEND - KHEAP_VSTART)

/* -------------------------------------------------------------------------
 * Magic number – placed in every block header.
 * If kfree() detects a different value the block has been corrupted (buffer
 * overflow or double-free) and the kernel panics.
 * ------------------------------------------------------------------------- */
#define KHEAP_MAGIC   0xDEADBEEFu

/* =========================================================================
 * Block header structure
 *
 * Lives immediately before the user-visible data pointer.
 * sizeof(block_header_t) must be kept constant; changing it invalidates
 * all existing heap pointers.
 * ========================================================================= */
typedef struct block_header {
    unsigned int         magic;   /**< Must be KHEAP_MAGIC (0xDEADBEEF)      */
    unsigned int         size;    /**< Bytes of DATA following this header    */
    int                  is_free; /**< 1 = free and available, 0 = allocated  */
    struct block_header *next;    /**< Next block in the linked list (or NULL)*/
    struct block_header *prev;    /**< Previous block (or NULL for first)     */
} block_header_t;

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * kheap_init – set up the kernel heap.
 *
 * Must be called once during boot (after pfa_init) before any kmalloc()
 * or kfree() calls.  Installs a single all-free block in the heap region.
 */
void kheap_init(void);

/**
 * kmalloc – allocate a contiguous block of kernel memory.
 *
 * Uses a first-fit search over the free-block list.  If the found block
 * is large enough, it is split so the remainder is returned to the pool.
 *
 * @param size  Number of bytes requested (must be > 0).
 * @return      Pointer to the data area of the allocated block, or NULL
 *              if the heap is exhausted.
 */
void *kmalloc(unsigned int size);

/**
 * kfree – release a previously allocated kernel memory block.
 *
 * Validates the block's magic number (panics on corruption), marks the
 * block as free, and coalesces it with any free adjacent blocks.
 *
 * @param ptr  Pointer returned by a previous kmalloc() call.
 *             Passing NULL is a no-op.
 */
void kfree(void *ptr);

/**
 * kheap_dump – log the current heap block list for debugging.
 *
 * Walks every block and emits one log line per block with its address,
 * size, and status.  Useful for diagnosing fragmentation or leaks.
 */
void kheap_dump(void);

/* =========================================================================
 * Test suite entry point
 * ========================================================================= */

/**
 * kheap_run_tests - Run the kheap unit-test suite via the ktest framework.
 * Call after kheap_init(), ktest_init() must have been called first.
 */
void kheap_run_tests(void);

#endif /* KHEAP_H */
