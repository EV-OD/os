/* =========================================================================
 * kheap.c – Kernel Heap Allocator  (first-fit + split + coalesce)
 *
 * This module implements the classic "boundary-tag" heap allocator described
 * by Kernighan & Ritchie (The C Programming Language, §8.7) adapted for an
 * OS kernel:
 *
 *   • No sbrk/brk – the heap lives entirely within the initial 4 MB kernel
 *     PSE mapping, so no extra page tables are required.
 *   • Magic-number guard – each block header is stamped with KHEAP_MAGIC
 *     (0xDEADBEEF).  kfree() verifies this before proceeding and calls
 *     kpanic() on mismatch, catching buffer-overflows and double-frees.
 *   • Block splitting – when the found free block is larger than needed,
 *     the remainder is carved into a new block and kept in the free list.
 *   • Block coalescing (merging) – after every free, both the next and
 *     the previous neighbour are checked; any free neighbour is merged.
 *     This prevents the "many small holes" fragmentation pathology.
 *
 * Memory layout of one allocated block (addresses increase downward)
 * -----------------------------------------------------------------
 *   +---[ block_header_t (20 bytes) ]---+   ← hptr
 *   |  magic    (4 B)  0xDEADBEEF       |
 *   |  size     (4 B)  data bytes only  |
 *   |  is_free  (4 B)  0 (allocated)    |
 *   |  next     (4 B)  next header ptr  |
 *   |  prev     (4 B)  prev header ptr  |
 *   +---[ user data  (size bytes) ]-----+   ← ptr returned to caller
 *   |  ...                              |
 *   +-----------------------------------+
 *
 * References
 * ----------
 *   • Course notes §10.3 "A Kernel Heap"
 *   • K&R §8.7 – "Example − A Storage Allocator"
 * ========================================================================= */

#include "kheap.h"
#include "log.h"

/* =========================================================================
 * Internal state
 * ========================================================================= */

/** Pointer to the first block header in the heap. */
static block_header_t *heap_head = (block_header_t *)0;

/* =========================================================================
 * kpanic – minimal kernel panic helper (halt + log).
 *
 * Called when an unrecoverable heap corruption is detected (bad magic).
 * Cannot use the heap itself (it may be corrupt), so prints directly via
 * the log subsystem and spins forever.
 * ========================================================================= */
static void kpanic(char *msg)
{
    log_error("[kheap] KERNEL PANIC: %s", msg);
    /* Disable interrupts and halt. */
    __asm__ volatile("cli; hlt");
    while (1) {}  /* unreachable, but satisfies -Wall */
}

/* =========================================================================
 * kheap_init – one-time heap setup.
 * ========================================================================= */
void kheap_init(void)
{
    block_header_t *first;

    /*
     * Place a single all-free block that spans the entire heap region.
     * The data size is the full region minus the header overhead.
     */
    first = (block_header_t *)KHEAP_VSTART;
    first->magic   = KHEAP_MAGIC;
    first->size    = KHEAP_SIZE - sizeof(block_header_t);
    first->is_free = 1;
    first->next    = (block_header_t *)0;
    first->prev    = (block_header_t *)0;

    heap_head = first;

    log_info("[kheap] initialised: virt [0x%x – 0x%x) total %u bytes",
             KHEAP_VSTART, KHEAP_VEND, KHEAP_SIZE);
}

/* =========================================================================
 * find_free_block – first-fit search for a free block of at least `size`.
 *
 * Returns a pointer to the matching block header, or NULL if no such block
 * exists in the current heap.
 * ========================================================================= */
static block_header_t *find_free_block(unsigned int size)
{
    block_header_t *cur = heap_head;

    while (cur) {
        if (cur->is_free && cur->size >= size) {
            return cur;
        }
        cur = cur->next;
    }
    return (block_header_t *)0;
}

/* =========================================================================
 * split_block – split a large free block into two smaller blocks.
 *
 * Precondition: hptr->size > size + sizeof(block_header_t)
 *               (i.e. there is room for a new header in the remainder).
 *
 * After the split:
 *   hptr          – size is reduced to `size`.
 *   hptr->next    – new free block covering the remainder.
 * ========================================================================= */
static void split_block(block_header_t *hptr, unsigned int size)
{
    block_header_t *new_block;
    unsigned char  *data_start;

    /* The new block header immediately follows hptr's data area. */
    data_start = (unsigned char *)hptr + sizeof(block_header_t);
    new_block  = (block_header_t *)(data_start + size);

    new_block->magic   = KHEAP_MAGIC;
    new_block->size    = hptr->size - size - sizeof(block_header_t);
    new_block->is_free = 1;
    new_block->next    = hptr->next;
    new_block->prev    = hptr;

    /* Relink: if there was a block after hptr, update its prev pointer. */
    if (hptr->next) {
        hptr->next->prev = new_block;
    }

    hptr->size = size;
    hptr->next = new_block;

    log_debug("[kheap] split: hptr=0x%x size=%u | new=0x%x size=%u",
              (unsigned int)hptr, hptr->size,
              (unsigned int)new_block, new_block->size);
}

/* =========================================================================
 * coalesce – merge hptr with adjacent free blocks.
 *
 * Called from kfree() after marking a block as free.  Checks both the
 * next and the previous neighbour and merges them if they are free.
 *
 * Merge order: always merge "next" before "prev" so that hptr stays
 * valid after the first merge.
 * ========================================================================= */
static void coalesce(block_header_t *hptr)
{
    block_header_t *next = hptr->next;
    block_header_t *prev = hptr->prev;

    /* --- Merge with next block if it is free -------------------------------- */
    if (next && next->is_free) {
        /*
         * Absorb next into hptr:
         *   new size = hptr->size + sizeof(header) + next->size
         */
        hptr->size += sizeof(block_header_t) + next->size;
        hptr->next  = next->next;
        if (next->next) {
            next->next->prev = hptr;
        }
        log_debug("[kheap] coalesce: merged next  @ 0x%x → new size=%u",
                  (unsigned int)hptr, hptr->size);
    }

    /* --- Merge with previous block if it is free ---------------------------- */
    if (prev && prev->is_free) {
        /*
         * Absorb hptr into prev:
         *   new size = prev->size + sizeof(header) + hptr->size
         */
        prev->size += sizeof(block_header_t) + hptr->size;
        prev->next  = hptr->next;
        if (hptr->next) {
            hptr->next->prev = prev;
        }
        log_debug("[kheap] coalesce: merged into prev @ 0x%x → new size=%u",
                  (unsigned int)prev, prev->size);
    }
}

/* =========================================================================
 * kmalloc – allocate a contiguous block of kernel memory.
 * ========================================================================= */
void *kmalloc(unsigned int size)
{
    block_header_t *hptr;
    unsigned char  *data_ptr;

    if (size == 0) {
        return (void *)0;
    }

    /* Align the requested size to 4 bytes for natural alignment. */
    size = (size + 3u) & ~3u;

    hptr = find_free_block(size);
    if (!hptr) {
        log_error("[kheap] kmalloc(%u): out of heap memory", size);
        return (void *)0;
    }

    /*
     * Only split if the remainder would be large enough to hold a
     * useful block (at least 1 byte of data + the header overhead).
     */
    if (hptr->size > size + sizeof(block_header_t) + 4u) {
        split_block(hptr, size);
    }

    hptr->is_free = 0;

    /* User pointer is immediately after the header. */
    data_ptr = (unsigned char *)hptr + sizeof(block_header_t);

    log_debug("[kheap] kmalloc(%u) → 0x%x  [hptr=0x%x]",
              size, (unsigned int)data_ptr, (unsigned int)hptr);

    return (void *)data_ptr;
}

/* =========================================================================
 * kfree – release a previously allocated block.
 * ========================================================================= */
void kfree(void *ptr)
{
    block_header_t *hptr;

    if (!ptr) {
        return;   /* freeing NULL is a documented no-op */
    }

    /* Walk backwards from the user pointer to recover the header. */
    hptr = (block_header_t *)((unsigned char *)ptr - sizeof(block_header_t));

    /* ------------------------------------------------------------------
     * Magic-number integrity check.
     * If this triggers, a buffer overflow (write past the end of a
     * previous block) or a double-free has corrupted the header.
     * ------------------------------------------------------------------ */
    if (hptr->magic != KHEAP_MAGIC) {
        kpanic("kfree: heap corruption – bad magic number (buffer overflow?)");
        return;  /* unreachable */
    }

    log_debug("[kheap] kfree(0x%x)  size=%u  [hptr=0x%x]",
              (unsigned int)ptr, hptr->size, (unsigned int)hptr);

    hptr->is_free = 1;

    /* Merge with free neighbours to prevent fragmentation. */
    coalesce(hptr);
}

/* =========================================================================
 * kheap_dump – walk all blocks and log their state (debugging aid).
 * ========================================================================= */
void kheap_dump(void)
{
    block_header_t *cur = heap_head;
    unsigned int    n   = 0;

    log_info("[kheap] === heap dump ===");
    while (cur) {
        log_info("[kheap]  [%u] hptr=0x%x  size=%-6u  %s  magic=%s",
                 n,
                 (unsigned int)cur,
                 cur->size,
                 cur->is_free ? "FREE" : "USED",
                 (cur->magic == KHEAP_MAGIC) ? "OK" : "CORRUPT!");
        n++;
        cur = cur->next;
    }
    log_info("[kheap] === %u blocks total ===", n);
}
