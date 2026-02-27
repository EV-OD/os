# Paging — Higher-Half Kernel with 4 MB Pages

This document describes how the kernel sets up paging to run at a higher-half virtual address (0xC0000000+) using 4 MB PSE pages. The setup spans two files: the initial page directory in `asm/loader.s` and the runtime helpers in `c_files/src/paging.c`.

## Overview

RandomOS uses a **higher-half kernel** design: the kernel code and data are linked at virtual address 0xC0100000 but physically loaded at 0x00100000 (the 1 MB mark) by GRUB. A minimal page directory is built at assembly time so that paging can be enabled before any C code runs.

## Page Directory (loader.s)

The page directory lives in the `.data` section, aligned to 4096 bytes. It contains 1024 32-bit entries. Only two are initially populated:

| Index | Virtual Range | Physical Range | Purpose |
|-------|---------------|----------------|---------|
| 0     | 0x00000000 – 0x003FFFFF | 0x00000000 – 0x003FFFFF | Identity map — needed so the instruction right after enabling CR0.PG can still be fetched at the physical EIP. Removed after the jump to `higher_half`. |
| 768   | 0xC0000000 – 0xC03FFFFF | 0x00000000 – 0x003FFFFF | Higher-half map — permanent kernel mapping. |

Each present entry has the value `0x00000083`:
- Bit 0 (P) = 1 — present.
- Bit 1 (R/W) = 1 — read/write.
- Bit 7 (PS) = 1 — 4 MB page (requires CR4.PSE).
- Bits 31-22 = 0 — frame 0 (physical 0x00000000).

## Loader Sequence

1. Save GRUB's `eax` (magic) and `ebx` (multiboot info pointer) into `esi`/`edi`.
2. Load the **physical** address of `page_directory` into CR3 (subtract `KERNEL_VIRTUAL_BASE` since the symbol is linked at a virtual address).
3. Set **CR4.PSE** (bit 4) to enable 4 MB page support.
4. Set **CR0.PG** (bit 31) to turn on paging. The identity-mapped entry 0 ensures the next fetch succeeds.
5. Jump to `higher_half` — a label linked at 0xC01XXXXX. After this jump, EIP is in the higher-half range.
6. Clear PDE entry 0 and `invlpg [0]` to remove the identity mapping.
7. Switch to the kernel stack (`kernel_stack_top`, 16 KB in `.bss`).
8. Adjust `edi` (multiboot info pointer) to its virtual address and call `kmain`.

## Linker Script (link.ld)

The linker places all sections at virtual addresses starting from `0xC0100000`. Each section uses an `AT()` directive so GRUB loads the physical data at `virtual_address - 0xC0000000`:

```
. = 0xC0100000;
.text ALIGN(0x1000) : AT(ADDR(.text) - KERNEL_VIRTUAL_BASE) { ... }
```

Two symbols are exported for C use:
- `kernel_physical_end` — physical address of the end of the kernel image.
- `kernel_virtual_end` — virtual address of the end of the kernel image.

## Runtime Helpers (paging.c / paging.h)

### `paging_init()`

Called during `kernel_init()`. Reads CR0, CR3, and CR4 and logs the paging status:
- Confirms paging is enabled (CR0 bit 31).
- Reports CR3 value (page directory physical address).
- Checks whether PSE is on (4 MB vs 4 KB pages).
- Verifies PDE[0] is cleared (identity map removed) and PDE[768] is present.

### `paging_cr3()`

Returns the current value of CR3 (the physical address of the active page directory).

### `paging_invlpg(void *vaddr)`

Invalidates the TLB entry for a single virtual address. Must be called after modifying a page directory entry to ensure the CPU uses the updated mapping.

### `paging_map_4mb(unsigned int index, unsigned int phys_frame, unsigned int flags)`

Maps a 4 MB page at page directory index `index` to physical frame `phys_frame`. Writes the PDE, flushes the TLB for that virtual address, and logs the mapping.

## Key Constants (paging.h)

| Constant | Value | Meaning |
|----------|-------|---------|
| `PDE_PRESENT` | 1 << 0 | Page is present in memory |
| `PDE_WRITABLE` | 1 << 1 | Page is read/write |
| `PDE_USER` | 1 << 2 | User-mode accessible |
| `PDE_PAGE_SIZE` | 1 << 7 | 4 MB page (PSE) |
| `PDE_4MB_RW` | PRESENT \| WRITABLE \| PAGE_SIZE | Common 4 MB read-write entry |
| `PAGE_SIZE_4MB` | 0x400000 | 4 MB in bytes |
| `PAGE_DIR_ENTRIES` | 1024 | Entries in a page directory |
| `KERNEL_VIRTUAL_BASE` | 0xC0000000 | Start of the higher-half region |
| `KERNEL_PAGE_INDEX` | 768 | PDE index for 0xC0000000 |
| `PHYS_TO_VIRT(p)` | p + 0xC0000000 | Convert physical to virtual address |
| `VIRT_TO_PHYS(v)` | v - 0xC0000000 | Convert virtual to physical address |

## Why 4 MB Pages?

- Simpler: only a single-level page directory needed (no page tables).
- The kernel currently fits well within 4 MB.
- Fewer TLB misses for the kernel's working set.
- When finer-grained 4 KB paging is needed (e.g. user-mode processes), page tables can be added per-entry.

## Linker-Script Boundary Symbols

The linker script now exports four symbols so C code can determine the
exact physical and virtual extents of the kernel image:

| Symbol | Value | Use |
|--------|-------|-----|
| `kernel_virtual_start` | `0xC0100000` | First virtual byte of the kernel |
| `kernel_physical_start` | `0x00100000` | First physical byte of the kernel |
| `kernel_virtual_end` | `0xC01XXXXX` | One past the last virtual byte |
| `kernel_physical_end` | `0x001XXXXX` | One past the last physical byte |

These symbols are used by `pfa_init()` to mark the kernel image frames as
reserved so they are never handed out as free memory.

## Temporary Mapping Window (for new Page Tables)

`pfa_alloc_frame()` returns a physical address.  With paging enabled, that
address cannot be written to directly — the CPU needs a virtual mapping.

The reserved slot at the end of the kernel page table solves this:

```
Virtual slot: (768 << 22) | (1023 << 12) = 0xC03FF000
```

To initialise a new physical frame as a page table:

1. Write the physical address into `page_table[1023]` with `PRESENT | WRITABLE`.
2. `invlpg(0xC03FF000)` — flush the stale TLB entry.
3. Write through `0xC03FF000` (CPU transparently redirects to the physical frame).
4. Clear `page_table[1023]` and `invlpg` again to remove the window.
5. Install the newly-prepared frame in the page directory.

This breaks the circular "I need a mapping to create a mapping" dependency.
See `docs/architecture/pfa.md §5` for the full worked example.

## Future Work

- Add 4 KB page table support for user-mode address spaces.
- Enable NX (No-Execute) bit when moving to PAE paging.
- Implement demand paging and page fault handling (ISR 14).
- Map additional memory regions (e.g. device MMIO above 4 MB).
- Wire `pfa_alloc_frame()` + temporary mapping into `paging_map_4kb()`.
