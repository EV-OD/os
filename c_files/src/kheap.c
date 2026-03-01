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

/* linker-exported symbol: virtual address just past the kernel .bss section */
extern unsigned int kernel_virtual_end;

/* =========================================================================
 * Internal state
 * ========================================================================= */

/** Pointer to the first block header in the heap (linked list head). */
static block_header_t *heap_head = (block_header_t *)0;

/** Virtual address of the current committed heap end.
 *  Memory between heap_head and heap_end is accessible (backed by PSE pages).
 *  kheap_expand() advances this toward KHEAP_VEND when more space is needed. */
static unsigned int heap_end = 0;

/* forward declarations */
static void coalesce(block_header_t *hptr);
static int  kheap_expand(unsigned int min_size);

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
    unsigned int    hstart;

    /*
     * Derive heap start from the linker-exported kernel_virtual_end symbol,
     * page-aligned upward.  This ensures the heap never overlaps kernel BSS
     * (critical when large compiler static arrays are in .bss).
     */
    hstart = ((unsigned int)&kernel_virtual_end + 0xFFFu) & ~0xFFFu;
    if (hstart < KHEAP_VSTART_DEFAULT)
        hstart = KHEAP_VSTART_DEFAULT;

    if (hstart >= KHEAP_VEND) {
        log_error("[kheap] kernel image too large: BSS end=0x%x >= KHEAP_VEND=0x%x –– heap disabled",
                  (unsigned int)&kernel_virtual_end, KHEAP_VEND);
        return;
    }

    /*
     * Commit the initial fixed block.  The virtual range [hstart, KHEAP_VEND)
     * is already backed by 4 MB PSE pages installed by paging_map_full_kernel_ram()
     * at boot, so no page allocation is needed here.
     */
    first = (block_header_t *)hstart;
    first->magic   = KHEAP_MAGIC;
    first->size    = KHEAP_INITIAL_SIZE - (unsigned int)sizeof(block_header_t);
    first->is_free = 1;
    first->next    = (block_header_t *)0;
    first->prev    = (block_header_t *)0;

    heap_head = first;
    heap_end  = hstart + KHEAP_INITIAL_SIZE;

    log_info("[kheap] init: start=0x%x end=0x%x initial=%u KB  capacity=%u KB",
             hstart, KHEAP_VEND,
             KHEAP_INITIAL_SIZE / 1024u,
             (KHEAP_VEND - hstart) / 1024u);
}

/* =========================================================================
 * kheap_expand – grow the heap by mapping more of the pre-reserved virtual
 *               address range into the free-block list.
 *
 * The virtual range [heap_end, KHEAP_VEND) is already backed by 4 MB PSE
 * pages set up at boot by paging_map_full_kernel_ram(), so expansion only
 * requires linking a new free block into the list – no PFA/page-table
 * allocation needed.
 *
 * Returns 0 on success, -1 when no virtual space remains.
 * ========================================================================= */
static int kheap_expand(unsigned int min_size)
{
    block_header_t *new_blk;
    block_header_t *tail;
    unsigned int    expand;

    /* Choose an expansion size: at least KHEAP_EXPAND_SIZE or enough for request */
    expand = KHEAP_EXPAND_SIZE;
    if (min_size + (unsigned int)sizeof(block_header_t) > expand)
        expand = (min_size + (unsigned int)sizeof(block_header_t) + 0xFFFu) & ~0xFFFu;

    /* Check virtual address budget */
    if (heap_end >= KHEAP_VEND) {
        log_error("[kheap] kheap_expand: virtual space exhausted (heap_end=0x%x)", heap_end);
        return -1;
    }
    if (expand > KHEAP_VEND - heap_end)
        expand = KHEAP_VEND - heap_end;
    if (expand < (unsigned int)sizeof(block_header_t) + 4u)
        return -1;

    /* Carve new free block at the current heap end */
    new_blk = (block_header_t *)heap_end;
    new_blk->magic   = KHEAP_MAGIC;
    new_blk->size    = expand - (unsigned int)sizeof(block_header_t);
    new_blk->is_free = 1;
    new_blk->next    = (block_header_t *)0;
    new_blk->prev    = (block_header_t *)0;

    /* Append to the tail of the free-block list */
    if (!heap_head) {
        heap_head = new_blk;
    } else {
        tail = heap_head;
        while (tail->next) tail = tail->next;
        tail->next    = new_blk;
        new_blk->prev = tail;
        /* Merge with tail if it is free (avoids fragmentation at the boundary) */
        if (tail->is_free)
            coalesce(tail);
    }

    heap_end += expand;
    log_info("[kheap] expanded +%u KB  →  heap_end=0x%x  (%u KB remaining)",
             expand / 1024u, heap_end, (KHEAP_VEND - heap_end) / 1024u);
    return 0;
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

    log_trace("[kheap] split: hptr=0x%x size=%u | new=0x%x size=%u",
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
        log_trace("[kheap] coalesce: merged next  @ 0x%x → new size=%u",
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
        log_trace("[kheap] coalesce: merged into prev @ 0x%x → new size=%u",
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
        /* Heap exhausted — try to grow before failing */
        if (kheap_expand(size) == 0)
            hptr = find_free_block(size);
    }
    if (!hptr) {
        log_error("[kheap] kmalloc(%u): heap exhausted (end=0x%x max=0x%x)",
                  size, heap_end, (unsigned int)KHEAP_VEND);
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

    log_trace("[kheap] kmalloc(%u) → 0x%x  [hptr=0x%x]",
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

    log_trace("[kheap] kfree(0x%x)  size=%u  [hptr=0x%x]",
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
