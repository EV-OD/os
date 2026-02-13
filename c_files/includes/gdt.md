# Global Descriptor Table (GDT)

This document records every design choice behind the GDT used by the kernel, explains the meaning of each flag and constant, and lists the current hard-coded values together with the rationale for keeping them.

## Why We Maintain a GDT

The x86 protected-mode CPU uses a Global Descriptor Table (GDT) to translate segment selectors into base addresses, limits, and access rules. Even when we run the kernel with a flat memory model, the CPU still requires a valid GDT before any segment register is loaded. Without it `lgdt` fails, instructions like `mov %ax, %ds` fault, and interrupts cannot use code/data segments safely.

## Descriptor Layout Recap

Each GDT entry is an 8-byte structure laid out as shown in `struct gdt_entry`:

| Bits        | Field          | Source in code | Notes |
|-------------|----------------|----------------|-------|
| 0-15        | limit_low      | `limit & 0xFFFF` | Lower 16 bits of the segment size ceiling. |
| 16-31       | base_low       | `base & 0xFFFF` | Lower 16 bits of the linear base address. |
| 32-39       | base_middle    | `(base >> 16) & 0xFF` | Base bits 16-23. |
| 40-47       | access         | Argument `access` | Encodes presence, privilege, descriptor type, and segment type. |
| 48-51       | limit high     | `(limit >> 16) & 0x0F` | Remaining limit bits. |
| 52          | AVL            | `(gran & 0x10)` | Available to software, unused here. |
| 53          | L              | `(gran & 0x20)` | Must be 0 in 32-bit mode. |
| 54          | D/B            | `(gran & 0x40)` | 1 enables 32-bit operand/address size. |
| 55          | G              | `(gran & 0x80)` | 1 scales the limit by 4 KiB pages. |
| 56-63       | base_high      | `(base >> 24) & 0xFF` | Base bits 24-31. |

The helper `gdt_set_entry` in [c_files/src/gdt.c](c_files/src/gdt.c#L21-L58) writes these fields so that callers only provide the base, limit, access byte, and granularity byte.

## Access Byte Cheatsheet

We pass the access byte literally (0x9A or 0x92). Breaking it down helps when future segments are added:

| Bit | Mask | Meaning when set | Usage |
|-----|------|------------------|-------|
| 7   | 0x80 | Present (P)      | Must stay 1 for valid descriptors. |
| 6-5 | 0x60 | Descriptor Privilege Level (DPL) | 0 keeps segments ring 0 only. |
| 4   | 0x10 | Descriptor type (S) | 1 marks a code/data descriptor (not system). |
| 3   | 0x08 | Executable (E)   | 1 for code, 0 for data. |
| 2   | 0x04 | Direction/Conforming (DC) | 0 ensures kernel-only execution and upward-growing data segments. |
| 1   | 0x02 | Read/Write (RW)  | 1 allows reads from code, writes to data. |
| 0   | 0x01 | Accessed (A)     | CPU flips to 1 on use; we leave 0. |

## Granularity Byte Cheatsheet

The top half of the granularity byte carries flags, the bottom half stores limit bits 16-19. We currently OR these values together so a single byte argument sets both parts.

| Bits | Mask | Meaning | Current reason |
|------|------|---------|----------------|
| 7    | 0x80 | G (Page granularity) | 1 makes the 20-bit limit count 4 KiB pages, letting 0xFFFFF cover full 4 GiB. |
| 6    | 0x40 | D/B (Default operand size) | 1 selects 32-bit segments so instructions assume 32-bit operands/addressing. |
| 5    | 0x20 | L (Long mode) | 0 because we are not in 64-bit long mode. |
| 4    | 0x10 | AVL          | 0, feature unused. |
| 3-0  | 0x0F | Limit[19:16] | 0xF completes the 0xFFFFF limit value. |

## GDTR Structure

The GDTR pointer defined by `struct gdt_ptr` holds:

- `size`: `(sizeof(entry) * count) - 1`. With three descriptors, that is `(8 * 3) - 1 = 23 (0x17)`. The subtraction is required by the `lgdt` instruction.
- `address`: The linear memory address of the first descriptor (`&gdt[0]`). In C we cast it to `unsigned int` before handing it to assembly.

This pointer is prepared once inside `gdt_init` and then passed to the assembly helper `gdt_load`, implemented in [asm/gdt.s](asm/gdt.s) where the actual `lgdt` and segment register reloads happen.

## Current Descriptor Table

| Index | Selector | Purpose | Base | Limit | Access | Granularity | Notes |
|-------|----------|---------|------|-------|--------|-------------|-------|
| 0     | 0x00     | Null descriptor | 0x00000000 | 0x00000 | 0x00 | 0x00 | All zeros as mandated by Intel. Selecting it triggers a fault, which catches stray null pointers. |
| 1     | 0x08     | Kernel code | 0x00000000 | 0xFFFFF | 0x9A | 0xCF | Flat 4 GiB code segment, ring 0 only, executable and readable. The limit plus `G=1` means linear addresses wrap at 4 GiB for segmentation checks. |
| 2     | 0x10     | Kernel data | 0x00000000 | 0xFFFFF | 0x92 | 0xCF | Flat 4 GiB data/stack segment, ring 0, writable. Shares base/limit with the code segment to keep a flat memory model. |

### Why These Hard-Coded Values?

- **Base = 0** for both segments keeps a flat model where logical and linear addresses match. Any higher-half kernel work will modify this.
- **Limit = 0xFFFFF** together with `G=1` expands to a 4 GiB span, the maximum in 32-bit protected mode. This ensures segmentation does not accidentally clip physical memory the kernel wants.
- **Access 0x9A / 0x92** match the typical protected-mode template: present, ring 0, code readable and data writable. We deliberately keep DPL at 0 because the kernel has no user mode yet; additional descriptors will be added for ring 3 later.
- **Granularity 0xCF** enables 32-bit operands and page granularity while keeping long-mode disabled. The lower nibble stays 0xF to match the limit high bits.
- **Null descriptor** stays zeroed (`gdt_set_entry(0, 0, 0, 0, 0)`) to comply with the architecture and fail fast on erroneous selector loads.

## Interaction With Segment Registers

`gdt_init` finishes by calling `gdt_load((unsigned int)&gp)`. The assembly routine performs:

1. `lgdt [gp]` — loads GDTR with size/address prepared above.
2. A far jump to reload `cs` with selector 0x08.
3. Reloads `ds`, `es`, `fs`, `gs`, and `ss` with selector 0x10.

These steps realign every segment register to the new descriptors, ensuring the CPU starts executing with the intended privilege and limit configuration.

## Future Extension Checklist

- User-mode support requires ring-3 copies of code/data descriptors (DPL bits set to 3, selectors 0x18 and 0x20).
- Task State Segment (TSS) descriptors live in the GDT as system entries; reserve space for them when multitasking is introduced.
- If Physical Address Extension (PAE) or x86-64 are considered, the granularity and long-mode bits need revision and the pointer structure changes size.
