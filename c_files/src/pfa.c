/* =========================================================================
 * pfa.c – Physical (Page) Frame Allocator  (bitmap implementation)
 *
 * Design decisions
 * ----------------
 * • Bitmap lives in .bss (zero-initialised by the loader).  Each bit
 *   represents one 4 KB physical frame: 0 = free, 1 = used.
 * • The bitmap is written as an array of 32-bit words so that the
 *   first_free() inner loop can skip fully-used words in one comparison.
 * • pfa_init() starts pessimistically: everything is "used".  It then
 *   opens only the regions the Multiboot mmap explicitly reports as
 *   AVAILABLE, and immediately re-closes the first 1 MB plus the kernel
 *   image range.  This guarantees that holes in the mmap (reserved,
 *   ACPI, MMIO …) are never handed out.
 *
 * Memory layout (typical 32-bit machine with 32 MB QEMU default)
 * ---------------------------------------------------------------
 *   0x00000000 – 0x000FFFFF  (1 MB)   BIOS/ROM/I-O – always reserved
 *   0x00100000 – kernel_physical_end   Kernel image – reserved by init
 *   kernel_physical_end – 0x01FFFFFF  Free physical RAM
 *
 * References
 * ----------
 *   • Course notes §10 "Page Frame Allocation"
 *   • OSDev wiki "Page Frame Allocator"
 *   • Intel SDM Vol. 3A Chapter 4 (Paging)
 * ========================================================================= */

#include "pfa.h"
#include "log.h"

/* -------------------------------------------------------------------------
 * Internal bitmap storage
 *
 * Placed in .bss – zeroed by the loader *before* we start.  We immediately
 * set every bit to 1 (used) inside pfa_init() as the safe default, then
 * selectively clear bits for available frames.
 * ------------------------------------------------------------------------- */
static unsigned int pfa_bitmap[PFA_BITMAP_WORDS];

/* Track the total number of frames known to this PFA instance. */
static unsigned int pfa_total_frames = 0;

/* =========================================================================
 * Static helpers – bitmap bit manipulation
 * ========================================================================= */

/*
 * frame_index  – convert a physical address to a frame index.
 *   index = physical_addr / FRAME_SIZE
 */
static inline unsigned int frame_index(unsigned int phys_addr)
{
    return phys_addr / FRAME_SIZE;
}

/*
 * bitmap_word  – which uint32_t word contains frame i's bit.
 *   word = i / 32
 */
static inline unsigned int bitmap_word(unsigned int index)
{
    return index / 32u;
}

/*
 * bitmap_bit   – which bit inside that word.
 *   bit = i % 32  →  mask = 1 << bit
 */
static inline unsigned int bitmap_mask(unsigned int index)
{
    return 1u << (index % 32u);
}

/* =========================================================================
 * Low-level public bitmap helpers
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * pfa_set_frame – mark a 4 KB frame as "used" (bit = 1).
 * ------------------------------------------------------------------------- */
void pfa_set_frame(unsigned int phys_addr)
{
    unsigned int idx  = frame_index(phys_addr);
    unsigned int word = bitmap_word(idx);
    unsigned int mask = bitmap_mask(idx);

    if (word < PFA_BITMAP_WORDS) {
        pfa_bitmap[word] |= mask;
    }
}

/* -------------------------------------------------------------------------
 * pfa_clear_frame – mark a 4 KB frame as "free" (bit = 0).
 * ------------------------------------------------------------------------- */
void pfa_clear_frame(unsigned int phys_addr)
{
    unsigned int idx  = frame_index(phys_addr);
    unsigned int word = bitmap_word(idx);
    unsigned int mask = bitmap_mask(idx);

    if (word < PFA_BITMAP_WORDS) {
        pfa_bitmap[word] &= ~mask;
    }
}

/* -------------------------------------------------------------------------
 * pfa_test_frame – query whether a frame is used.
 * Returns 1 if used, 0 if free.
 * ------------------------------------------------------------------------- */
int pfa_test_frame(unsigned int phys_addr)
{
    unsigned int idx  = frame_index(phys_addr);
    unsigned int word = bitmap_word(idx);
    unsigned int mask = bitmap_mask(idx);

    if (word >= PFA_BITMAP_WORDS) {
        return 1;  /* treat out-of-range as "used" */
    }
    return (pfa_bitmap[word] & mask) ? 1 : 0;
}

/* =========================================================================
 * first_free – scan bitmap for the first bit that is 0 (free).
 *
 * Inner optimisation: if an entire 32-bit word equals 0xFFFFFFFF, all 32
 * frames it represents are full – skip to the next word immediately.
 *
 * Returns the frame index, or PFA_MAX_FRAMES if none is found.
 * ========================================================================= */
static unsigned int first_free(void)
{
    unsigned int w, b;

    for (w = 0; w < PFA_BITMAP_WORDS; w++) {
        if (pfa_bitmap[w] == 0xFFFFFFFFu) {
            continue;   /* all 32 frames in this word are used */
        }
        /* At least one free frame in this word – find which bit. */
        for (b = 0; b < 32u; b++) {
            if (!(pfa_bitmap[w] & (1u << b))) {
                return w * 32u + b;   /* frame index */
            }
        }
    }
    return PFA_MAX_FRAMES;   /* no free frame found */
}

/* =========================================================================
 * pfa_init – parse Multiboot mmap and initialise the bitmap.
 * ========================================================================= */
void pfa_init(multiboot_info_t *mb,
              unsigned int phys_kernel_start,
              unsigned int phys_kernel_end)
{
    multiboot_memory_map_t *mmap;
    unsigned int mmap_end;
    unsigned int region_start, region_end, addr;
    unsigned int total_kb = 0;

    /* ------------------------------------------------------------------
     * Step 1: Mark every frame as "used" (pessimistic default).
     *         We do this by filling every word with 0xFFFFFFFF.
     * ------------------------------------------------------------------ */
    {
        unsigned int w;
        for (w = 0; w < PFA_BITMAP_WORDS; w++) {
            pfa_bitmap[w] = 0xFFFFFFFFu;
        }
    }

    /* ------------------------------------------------------------------
     * Step 2: Walk the Multiboot memory map.  For every region of type
     *         MULTIBOOT_MEMORY_AVAILABLE, free all 4 KB frames within it.
     *
     *  mmap->addr and mmap->len are 64-bit, but on a 32-bit platform we
     *  only care about the lower 32 bits (we cannot address > 4 GB).
     * ------------------------------------------------------------------ */
    if (!(mb->flags & MULTIBOOT_INFO_MEM_MAP)) {
        log_warning("[pfa] Multiboot mmap not provided – cannot initialise PFA");
        return;
    }

    mmap     = (multiboot_memory_map_t *)(unsigned int)mb->mmap_addr;
    mmap_end = mb->mmap_addr + mb->mmap_length;

    while ((unsigned int)mmap < mmap_end) {

        if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {

            /* Clamp to 32-bit addressable range. */
            region_start = (unsigned int)(mmap->addr & 0xFFFFFFFFu);
            region_end   = region_start +
                           (unsigned int)(mmap->len  & 0xFFFFFFFFu);

            total_kb += (unsigned int)(mmap->len / 1024u);

            /* Free every aligned 4 KB frame in this region. */
            for (addr  = (region_start + FRAME_SIZE - 1u) & ~(FRAME_SIZE - 1u);
                 addr  < region_end;
                 addr += FRAME_SIZE) {

                pfa_clear_frame(addr);
                pfa_total_frames++;
            }
        }

        /* Advance to the next entry: size field does NOT include itself. */
        mmap = (multiboot_memory_map_t *)
               ((unsigned int)mmap + mmap->size + sizeof(mmap->size));
    }

    log_info("[pfa] total available RAM: ~%u KB (%u free frames)",
             total_kb, pfa_total_frames);

    /* ------------------------------------------------------------------
     * Step 3a: Re-mark the first 1 MB as "used".
     *          This covers the real-mode IVT, BIOS data area, VGA memory,
     *          BIOS ROM, and whatever GRUB put below 1 MB.
     * ------------------------------------------------------------------ */
    for (addr = 0; addr < 0x100000u; addr += FRAME_SIZE) {
        pfa_set_frame(addr);
    }

    /* ------------------------------------------------------------------
     * Step 3b: Re-mark the kernel image frames as "used".
     *          Align both boundaries to 4 KB so partial frames are
     *          also protected.
     * ------------------------------------------------------------------ */
    region_start = phys_kernel_start & ~(FRAME_SIZE - 1u);
    region_end   = (phys_kernel_end + FRAME_SIZE - 1u) & ~(FRAME_SIZE - 1u);

    for (addr = region_start; addr < region_end; addr += FRAME_SIZE) {
        pfa_set_frame(addr);
    }

    log_info("[pfa] reserved kernel [0x%x – 0x%x) physical",
             phys_kernel_start, phys_kernel_end);
    log_info("[pfa] free frames after init: %u (%u KB)",
             pfa_free_count(), pfa_free_count() * (FRAME_SIZE / 1024u));
}

/* =========================================================================
 * pfa_alloc_frame – allocate one free 4 KB physical frame.
 * ========================================================================= */
unsigned int pfa_alloc_frame(void)
{
    unsigned int idx  = first_free();
    unsigned int phys;

    if (idx == PFA_MAX_FRAMES) {
        log_error("[pfa] pfa_alloc_frame: OUT OF MEMORY");
        return PFA_ALLOC_FAIL;
    }

    /* Mark the frame as used and convert index → physical address. */
    pfa_bitmap[bitmap_word(idx)] |= bitmap_mask(idx);
    phys = idx * FRAME_SIZE;

    log_debug("[pfa] alloc frame @ phys 0x%x (index %u)", phys, idx);
    return phys;
}

/* =========================================================================
 * pfa_free_frame – return a 4 KB physical frame to the free pool.
 * ========================================================================= */
void pfa_free_frame(unsigned int phys_addr)
{
    unsigned int idx = frame_index(phys_addr);

    if (bitmap_word(idx) >= PFA_BITMAP_WORDS) {
        log_warning("[pfa] pfa_free_frame: address 0x%x out of range",
                    phys_addr);
        return;
    }

    pfa_clear_frame(phys_addr);
    log_debug("[pfa] free frame @ phys 0x%x (index %u)", phys_addr, idx);
}

/* =========================================================================
 * pfa_free_count – count the number of free frames (O(n) scan).
 *
 * Counts the number of zero bits in the bitmap by using the
 * "popcount of complement" trick: count set bits in ~word.
 * ========================================================================= */
unsigned int pfa_free_count(void)
{
    unsigned int count = 0;
    unsigned int w, word;

    for (w = 0; w < PFA_BITMAP_WORDS; w++) {
        word = ~pfa_bitmap[w];   /* invert: 1 now means "free" */
        /* Kernighan's bit-count trick */
        while (word) {
            word &= (word - 1u);
            count++;
        }
    }
    return count;
}
