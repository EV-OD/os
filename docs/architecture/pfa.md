# Physical Frame Allocator (PFA) – Buddy System

The PFA manages physical RAM at the granularity of 4 KB page frames.
It uses a **Buddy System** allocator — the same algorithm used by the Linux kernel.

---

## 1. Motivation: Why Buddy over Bitmap?

| Property | Flat Bitmap | Buddy System |
|----------|-------------|--------------|
| Allocation speed | O(n) scan | O(log n) – scan orders |
| Free / merge speed | O(1) | O(log n) – merge chain |
| Contiguous multi-frame alloc | Difficult | Natural (higher orders) |
| External fragmentation | High | Low (power-of-2 merging) |
| BSS footprint (128 MB) | 4 KB bitmap | 384 KB descriptors |

The bitmap is simpler to implement but the buddy system is necessary once you need contiguous allocations for DMA, page tables, and large objects.

---

## 2. How the Buddy System Works

### Block sizes (orders)

Physical RAM is divided into blocks whose size is always a power of two times
the base frame size (4 KB):

| Order | Block size | Frames |
|-------|-----------|--------|
| 0 | 4 KB | 1 |
| 1 | 8 KB | 2 |
| 2 | 16 KB | 4 |
| 3 | 32 KB | 8 |
| … | … | … |
| 11 | 8 MB | 2 048 |
| 18 | 1 GB | 262 144 |

A per-order **doubly-linked free list** tracks available blocks:

```c
static unsigned int free_lists[BUDDY_MAX_ORDER + 1];  /* head frame index */
```

### The XOR Buddy Formula

Every block has exactly one **buddy** — the adjacent block of the same size
it can merge with.  Given a block starting at frame index `f` at order `o`:

$$\text{buddy} = f \oplus 2^o$$

Example: frame 0 at order 3 (8 frames) → buddy = `0 XOR 8 = 8`. ✓  
Example: frame 8 at order 3 → buddy = `8 XOR 8 = 0`. ✓ (symmetric)

The XOR flips exactly bit `o` of the frame index, mapping each block to
its unique partner of the same size.

---

## 3. Allocation (split down)

Request: one 4 KB frame (order 0).

```
1. Scan free_lists[0], free_lists[1], ... until a non-empty list is found
   at order k.

2. Pop the head block from free_lists[k].

3. While k > 0:
     k--
     buddy = frame_idx + (1 << k)   // upper half of the split
     push buddy into free_lists[k]  // half goes back to pool
                                    // lower half continues splitting

4. Return frame_idx * FRAME_SIZE.
```

Example: only free_lists[3] is non-empty (an 8-frame block at frame 64):

```
Split order 3 → push frame 68 into free_lists[2]  (frames 68-71)
Split order 2 → push frame 66 into free_lists[1]  (frames 66-67)
Split order 1 → push frame 65 into free_lists[0]  (frame  65)
Return frame 64 (4 KB) to caller.
```

---

## 4. Deallocation (merge up)

Free a 4 KB frame at physical address `phys_addr`:

```
frame_idx = phys_addr / FRAME_SIZE
order = 0

loop:
    buddy = frame_idx XOR (1 << order)

    if buddy >= BUDDY_MAX_FRAMES         → stop (out of range)
    if !frames[buddy].present            → stop (buddy is not real RAM)
    if !frames[buddy].is_free            → stop (buddy is in use)
    if  frames[buddy].order != order     → stop (buddy is a different-size block)

    list_remove(order, buddy)
    frame_idx = min(frame_idx, buddy)    // merged block starts at lower addr
    order++

list_push(order, frame_idx)
```

The classic merge pattern for sequential frees 0,1,2,3,4,5,6,7…:

```
free 0 → order 0  (block [0])
free 1 → order 1  ([0,1] merged)
free 2 → order 0  (block [2])
free 3 → order 2  ([0,1,2,3] merged)
free 4 → order 0  ...
free 5 → order 1  ...
free 6 → order 0  ...
free 7 → order 3  ([0..7] merged)
```

This is exactly the binary carry pattern — each power of two triggers a cascade of merges upward.

---

## 5. Per-frame Descriptor (`frame_desc_t`)

```c
typedef struct {
    unsigned char  is_free;  /* 1 = head of a free block at 'order'  */
    unsigned char  order;    /* order of the block  (valid if is_free)*/
    unsigned char  present;  /* 1 = frame is backed by real hardware RAM */
    unsigned char  _pad;
    unsigned int   bl_next;  /* next free block frame index (BUDDY_NONE)*/
    unsigned int   bl_prev;  /* prev free block frame index (BUDDY_NONE)*/
} frame_desc_t;              /* sizeof = 12 bytes                        */
```

Only the **head** frame of a free block carries meaningful `is_free`, `order`,
`bl_next`, `bl_prev` values.  Interior frames have `is_free = 0`.

The `present` flag marks frames that exist in real hardware RAM (set during
`pfa_init` from the Multiboot mmap).  The merge loop checks this before
coalescing to avoid merging into MMIO holes.

**BSS footprint:** `32768 × 12 = 384 KB` for 128 MB of RAM.

---

## 6. Initialisation (`pfa_init`)

```
1. Set all free_list heads to BUDDY_NONE.
   (frame descriptors are zeroed by loader.s - .bss section)

2. Walk the Multiboot mmap.
   For each AVAILABLE region, for each 4 KB-aligned frame address addr:
     - Skip addr < 0x100000               (first 1 MB - BIOS/IO)
     - Skip addr in [kernel_start, kernel_end)  (kernel image)
     - Skip frame_idx >= BUDDY_MAX_FRAMES  (beyond tracking range)
     - Set frames[idx].present = 1
     - Call pfa_free_frame(addr)
       → inserts at order 0 and merges upward on the fly

3. After all frames are processed the free lists hold the largest
   power-of-2 blocks that tile the available physical RAM.
```

This "insert one-by-one with live merging" init is O(n log n) but simple and correct.  For ~8 000 frames (32 MB) the total work is ~100 K operations.

### Boot output (QEMU 32 MB)

```
[pfa] buddy init: kernel phys [0x100000, 0x18c2ac)
[pfa] total available RAM : ~32255 KB
[pfa]   order  0 :  1 block(s) x    4 KB =   4 KB free
[pfa]   order  1 :  1 block(s) x    8 KB =   8 KB free
[pfa]   order  4 :  1 block(s) x   64 KB =  64 KB free
[pfa]   order  5 :  2 block(s) x  128 KB = 256 KB free
[pfa]   order  6 :  2 block(s) x  256 KB = 512 KB free
[pfa]   order  7 :  1 block(s) x  512 KB = 512 KB free
[pfa]   order  8 :  1 block(s) x 1024 KB = 1024 KB free
[pfa]   order  9 :  2 block(s) x 2048 KB = 4096 KB free
[pfa]   order 10 :  2 block(s) x 4096 KB = 8192 KB free
[pfa]   order 11 :  2 block(s) x 8192 KB = 16384 KB free
[pfa] total free frames: 7763  (31052 KB)
```

---

## 7. Internal Fragmentation

The Buddy System suffers from **internal fragmentation**: a 5 KB request
must be served by an order-1 block (8 KB), wasting 3 KB.

Mitigation: the kernel heap (`kheap.c`) sits above the PFA and slices buddy
blocks into arbitrarily-sized chunks via a first-fit linked-list allocator.
This way the PFA wastes at most one block per `kmalloc` call.

---

## 8. Files

| File | Role |
|------|------|
| `c_files/includes/pfa.h` | Public API, `frame_desc_t`, constants |
| `c_files/src/pfa.c` | `pfa_init`, `pfa_alloc_frame`, `pfa_free_frame`, `free_lists`, `frames[]` |
| `linker/link.ld` | Exports `kernel_physical_start` / `kernel_physical_end` |

---

## 9. Future Work

- `pfa_alloc_frames(n)` — allocate `n` contiguous frames at the natural order (needed for DMA, page table creation).
- Per-NUMA-node free lists for multi-socket systems.
- `pfa_alloc_order(o)` — expose higher-order allocation directly to the paging subsystem for huge-page support.
