# IDT Gate/Descriptor Cheatsheet

## IDT entry layout (32-bit)
- offset_low  (16): handler address bits 0–15
- selector    (16): code segment selector (e.g., 0x08 kernel code)
- zero        (8) : must be 0
- type_attr   (8) : P | DPL | S | Type bits
- offset_high (16): handler address bits 16–31

## Type/attr bits (type_attr)
- Bit 7: P (present) — must be 1 for valid entry
- Bits 6–5: DPL — 0..3, caller privilege allowed
- Bit 4: S — should be 0 for gates
- Bits 3–0: gate type
  - 0xE = 32-bit interrupt gate (clears IF on entry)
  - 0xF = 32-bit trap gate (leaves IF unchanged)

Common kernel gate: **0x8E** = P=1, DPL=0, S=0, Type=0xE

## Selector
- Use kernel code selector (0x08) for ring 0 handlers.
- For user-callable gates (syscalls), set DPL=3 and still point at kernel code selector.

## Offset
- 32-bit handler address; set via `idt_set_gate(vector, handler, selector, flags)`.

## GDT access/granularity quick ref (for context)
- Access byte (code/data descriptors):
  - Bit7 P, Bits6–5 DPL, Bit4 S, Bits3–0 Type
  - Code: 0x9A (present, DPL0, code, readable)
  - Data: 0x92 (present, DPL0, data, writable)
- Granularity byte: Bits7 G (4 KiB pages), Bit6 D (32-bit), Bit5 L (0), Bit4 AVL (0), Bits3–0 limit[19:16]
  - Common: 0xCF with limit high nibble = 0xF for 4 GiB flat segments

## Setup recipe
1) Zero IDT entries, load IDTR via `lidt`.
2) For each vector, set gate to interrupt gate (0x8E) with selector 0x08 and handler address.
3) Remap PIC so IRQs sit at 0x20–0x2F, then unmask as needed.
4) Enable interrupts when ready.
