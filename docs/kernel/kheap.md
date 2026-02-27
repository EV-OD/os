# Kernel Heap (`kmalloc` / `kfree`)

The kernel heap provides dynamic memory allocation (`kmalloc` / `kfree`) above the physical frame allocator.  
It implements the classic **boundary-tag allocator** described by Kernighan & Ritchie (*The C Programming Language*, §8.7), adapted for a bare-metal kernel.

---

## 1. Heap Layout

The heap lives entirely within the initial **4 MB PSE kernel mapping** (PDE[768]), so no extra page tables are needed during boot.

```
Virtual address space
─────────────────────────────────────────────────────────
0xC0000000   Kernel mapping start  (PDE[768])
0xC0100000   Kernel image (.text, .rodata, .data, .bss)
...
0xC0300000   ← KHEAP_VSTART  (heap begins here)
             │
             │  ~1 MB of heap space
             │  managed as a doubly-linked list of blocks
             │
0xC03FF000   ← KHEAP_VEND    (heap ends here)
0xC0400000   End of initial 4 MB mapping
─────────────────────────────────────────────────────────
```

After `kheap_init()` there is exactly one block that covers the entire region and is marked **free**.

---

## 2. Block Header

Every heap allocation (free or used) is preceded by a `block_header_t`:

```c
typedef struct block_header {
    unsigned int         magic;    /* KHEAP_MAGIC = 0xDEADBEEF            */
    unsigned int         size;     /* bytes of DATA (not including header) */
    int                  is_free;  /* 1 = free, 0 = allocated             */
    struct block_header *next;     /* next block in the list  (or NULL)    */
    struct block_header *prev;     /* previous block          (or NULL)    */
} block_header_t;                  /* sizeof == 20 bytes                   */
```

### Memory layout of one block

```
┌─────────────────────────────────┐  ← hptr  (block_header_t*)
│ magic    4 B   0xDEADBEEF       │
│ size     4 B   data bytes only  │
│ is_free  4 B   0 or 1           │
│ next     4 B   pointer          │
│ prev     4 B   pointer          │
├─────────────────────────────────┤  ← ptr  (void* returned to caller)
│                                 │
│   user data  (size bytes)       │
│                                 │
└─────────────────────────────────┘
```

**hptr → ptr conversion:**

```c
/* malloc: user gets data area */
void *ptr = (void *)((char *)hptr + sizeof(block_header_t));

/* free: recover header from user pointer */
block_header_t *hptr = (block_header_t *)((char *)ptr - sizeof(block_header_t));
```

---

## 3. The Magic Number Guard (`0xDEADBEEF`)

Every block header is stamped with `KHEAP_MAGIC = 0xDEADBEEF` at allocation time.

`kfree()` checks this value **before** touching the block:

```c
if (hptr->magic != KHEAP_MAGIC) {
    kpanic("kfree: heap corruption – bad magic number");
}
```

If the value is wrong, a buffer overflow (writing past the end of an adjacent block) or a **double-free** has corrupted the header.  
The kernel panics immediately (`cli; hlt`) instead of silently continuing with a corrupt data structure.

---

## 4. `kmalloc` — First-Fit Allocation

```
1. Align requested size to 4 bytes.
2. Walk the block list from heap_head (first-fit search).
3. Find the first block where is_free == 1  AND  size >= requested.
4. If the block is large enough to split (remainder > header + 4 bytes):
       split_block(hptr, size)  → carve a new free block from the tail.
5. Mark hptr->is_free = 0.
6. Return ptr = hptr + sizeof(block_header_t).
```

### Block splitting

Prevents wasting memory when the found block is much larger than needed:

```
Before split:
  ┌─ hdr (size=4080) ─────────────────────────────────────┐
  │  FREE                                                  │
  └────────────────────────────────────────────────────────┘

After split (request for 128 bytes):
  ┌─ hdr (size=128) ──┐  ┌─ new_hdr (size=3932) ──────────┐
  │  USED             │  │  FREE                           │
  └───────────────────┘  └─────────────────────────────────┘
```

---

## 5. `kfree` — Free with Coalescing

```
1. Recover hptr from ptr (subtract header size).
2. Validate hptr->magic == KHEAP_MAGIC  (panic on mismatch).
3. Set hptr->is_free = 1.
4. coalesce(hptr):
     a. If hptr->next is free → absorb it (size += hdr + next->size; relink).
     b. If hptr->prev is free → absorb hptr into prev (prev->size += hdr + hptr->size; relink).
```

### Why coalescing matters

Without it, repeated alloc/free cycles produce many small holes that cannot satisfy larger requests even when the total free memory is sufficient:

```
Before coalesce:  [ 50 FREE ][ 50 FREE ][ 50 FREE ]  → cannot serve 100-byte request
After  coalesce:  [ 150 FREE                       ]  → can serve 100-byte request
```

---

## 6. `kheap_dump` — Debug Helper

```c
kheap_dump();
```

Walks every block and logs one line per block:

```
[kheap] === heap dump ===
[kheap]  [0] hptr=0xC0300000  size=1044460  FREE  magic=OK
[kheap] === 1 blocks total ===
```

Useful when investigating fragmentation or testing allocator correctness.

---

## 7. Boot Smoke Test

`kmain()` runs a quick self-test after `kheap_init()` returns:

```c
void *a = kmalloc(64);
void *b = kmalloc(128);
void *c = kmalloc(32);
kfree(b);   /* free middle  → coalesce gap */
kfree(a);   /* free left    → coalesce with gap */
kfree(c);   /* free right   → heap back to one block */
```

If the magic number or coalescing logic is broken, this test will produce a visible kernel panic on every boot rather than a silent corruption later.

---

## 8. Files

| File | Role |
|------|------|
| `c_files/includes/kheap.h` | Public API, `block_header_t`, constants |
| `c_files/src/kheap.c` | `kheap_init`, `kmalloc`, `kfree`, `coalesce`, `split_block`, `kheap_dump` |

---

## 9. Limitations and Future Work

| Limitation | Future solution |
|------------|-----------------|
| Fixed static heap region (1 MB) | Extend heap by calling `pfa_alloc_frame()` when the free list is exhausted, then map the new frame into the heap virtual range |
| First-fit can be slow on large heaps | Switch to a size-segregated free list or a best-fit policy |
| No thread safety | Add spinlock protection around `heap_head` access |
| Internal fragmentation (4-byte alignment) | Align to 8 or 16 bytes for SIMD-safe allocations |
