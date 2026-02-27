# Physical Frame Allocator (PFA)

The PFA answers one question: **"Give me the physical address of a free 4 KB block of RAM."**  
It is the foundation on which the kernel heap, virtual memory, and page-table management are built.

---

## 1. How Much Memory Is There?

When the kernel starts, the CPU has no built-in knowledge of how much RAM the machine has.
GRUB collects that information from the BIOS and passes it through the **Multiboot memory map** (mmap): an array of entries stored in the Multiboot info structure.

### Multiboot mmap entry

```c
struct multiboot_mmap_entry {
    uint32_t size;     /* size of this entry (not including this field) */
    uint64_t addr;     /* start of the memory region                   */
    uint64_t len;      /* length in bytes                               */
    uint32_t type;     /* AVAILABLE = 1, RESERVED = 2, ACPI = 3, …    */
} __attribute__((packed));
```

Only regions with `type == MULTIBOOT_MEMORY_AVAILABLE` can be used as ordinary RAM.

### The kernel image must be protected

Even "Available" regions contain the kernel's own code and data.  
If the PFA handed out a frame that holds `kmain`, the next write would overwrite the running kernel.

The linker script exports two **boundary symbols** to mark the kernel image:

```ld
kernel_virtual_start  = .;               /* virtual address of kernel start */
kernel_physical_start = . - 0xC0000000; /* physical address of kernel start */
/* … .text / .rodata / .data / .bss … */
kernel_virtual_end    = .;
kernel_physical_end   = . - 0xC0000000;
```

In C code these are referenced as:

```c
extern unsigned int kernel_physical_start;
extern unsigned int kernel_physical_end;

unsigned int phys_start = (unsigned int)&kernel_physical_start;
unsigned int phys_end   = (unsigned int)&kernel_physical_end;
```

> **Tip:** The symbols are not real variables. Taking `&symbol` gives you the symbol's *value* (the address), not the address of a storage location. No memory dereference occurs.

---

## 2. The Bitmap Data Structure

### Concept

One bit represents one 4 KB physical frame:

| Bit value | Meaning |
|-----------|---------|
| `0` | Frame is **free** |
| `1` | Frame is **used** |

### Storage

```c
/* pfa.c – static bitmap in .bss (zero-initialised by the loader) */
static unsigned int pfa_bitmap[PFA_BITMAP_WORDS];  /* 32 768 words = 128 KB */
```

| Constant | Value | Derivation |
|----------|-------|------------|
| `FRAME_SIZE` | 4 096 | 4 KB per frame |
| `PFA_MAX_FRAMES` | 1 048 576 | 4 GB / 4 KB |
| `PFA_BITMAP_WORDS` | 32 768 | 1 048 576 / 32 bits |

The bitmap can track up to **4 GB** of physical RAM at a cost of only **128 KB** of kernel BSS.

### Bit manipulation

```c
/* Convert a physical address to a frame index */
index = phys_addr / FRAME_SIZE;               /* e.g. 0x200000 / 0x1000 = 512 */

/* Which 32-bit word holds bit 'index'? */
word  = index / 32;                            /* e.g. 512 / 32 = 16           */

/* Which bit inside that word? */
mask  = 1u << (index % 32);                   /* e.g. 1 << (512 % 32) = 1     */

/* Mark used */
pfa_bitmap[word] |= mask;

/* Mark free */
pfa_bitmap[word] &= ~mask;

/* Test */
used = (pfa_bitmap[word] & mask) != 0;
```

---

## 3. Initialisation Algorithm (`pfa_init`)

```
1. Fill every bitmap word with 0xFFFFFFFF  → everything "used" (safe default)

2. Walk the Multiboot mmap:
     for each entry where type == AVAILABLE:
         for each 4 KB frame within that region:
             pfa_clear_frame(addr)            → mark free

3a. Re-mark the first 1 MB as used:
     for addr = 0x00000000 → 0x000FFFFF:
         pfa_set_frame(addr)   (BIOS, I/O, GRUB data, VGA VRAM)

3b. Re-mark the kernel image as used:
     for addr = phys_kernel_start → phys_kernel_end:
         pfa_set_frame(addr)
```

This "pessimistic start, selective open, re-close reserved" approach ensures holes in the mmap (MMIO, ACPI tables, …) are never given out even if the firmware lists them as available.

---

## 4. Core Functions

### `pfa_alloc_frame()`

```c
unsigned int pfa_alloc_frame(void);
```

Scans the bitmap for the first `0` bit.

**Optimisation:** if an entire 32-bit word equals `0xFFFFFFFF`, all 32 frames it covers are full — skip to the next word in one comparison instead of testing 32 individual bits.

Returns the **physical byte address** of the allocated frame, or `PFA_ALLOC_FAIL` (`0xFFFFFFFF`) if memory is exhausted.

### `pfa_free_frame(phys_addr)`

```c
void pfa_free_frame(unsigned int phys_addr);
```

Clears the bitmap bit for the given physical address, returning the frame to the free pool.

### `pfa_free_count()`

```c
unsigned int pfa_free_count(void);
```

Returns the number of free frames by counting zero bits (using Kernighan's bit-clearing trick on the inverted bitmap). Useful for boot diagnostics.

---

## 5. "Chicken-and-Egg": Accessing a New Frame

`pfa_alloc_frame()` returns a **physical** address.  
With paging enabled, the CPU only understands **virtual** addresses.

If a new frame needs to be used as a page table (to map other frames into virtual memory), you cannot write to it until it is itself mapped — a circular dependency.

### Solution: a temporary mapping window

The initial kernel page table (PDE[768], covering `0xC0000000–0xC03FFFFF`) has 1024 entries, one per 4 KB virtual page.  
Reserve the **last entry** (index 1023) as a temporary window:

```
Virtual slot: (768 << 22) | (1023 << 12) | 0 = 0xC03FF000
```

Workflow to turn a new physical frame into a page table:

```
1. pfa_alloc_frame()         → phys_addr (e.g. 0x00500000)
2. page_table[1023] = phys_addr | PTE_PRESENT | PTE_WRITABLE
3. invlpg(0xC03FF000)        → flush TLB for the window slot
4. memset((void*)0xC03FF000, 0, 4096)  → zero-initialise the new table
                                          (write goes to phys 0x00500000)
5. page_table[1023] = 0      → remove temporary mapping
6. page_directory[new_idx] = phys_addr | PDE_PRESENT | ...
                             → install the new page table permanently
```

---

## 6. Files

| File | Role |
|------|------|
| `c_files/includes/pfa.h` | Public API, constants, function declarations |
| `c_files/src/pfa.c` | Bitmap storage, `pfa_init`, `pfa_alloc_frame`, `pfa_free_frame` |
| `linker/link.ld` | Exports `kernel_physical_start` / `kernel_physical_end` |

---

## 7. Logged Output (boot)

```
[pfa] total available RAM: ~31744 KB (7680 free frames)
[pfa] reserved kernel [0x00100000 – 0x00205000) physical
[pfa] free frames after init: 7296 (28672 KB)
```

---

## Future Work

- **Buddy system** — replace the first-fit bitmap with an order-sorted free-list for O(log n) allocation and reduced fragmentation.
- **NUMA awareness** — maintain per-node bitmaps on multi-socket systems.
- **`pfa_alloc_region(n)`** — allocate `n` *contiguous* frames (needed for DMA, large page tables).
