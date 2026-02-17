# Interrupt Descriptor Table (IDT)

This note records how the kernel builds and loads the Interrupt Descriptor Table. It covers the descriptor layout, the gate flags we use, and the current initialization flow so future interrupt handlers can plug into the existing scaffolding.

## Entry Layout

Each entry is eight bytes. The structure in [c_files/includes/idt.h](c_files/includes/idt.h) matches the hardware layout exactly:

| Bits  | Field        | Meaning |
|-------|--------------|---------|
| 0-15  | offset_low   | Handler address bits 0-15. |
| 16-31 | selector     | GDT selector pointing at the code segment that runs the handler. |
| 32-39 | zero         | Must stay 0. |
| 40-47 | type_attr    | Present bit, privilege level, and gate type (interrupt/trap). |
| 48-63 | offset_high  | Handler address bits 16-31. |

The pointer structure `struct idt_ptr` holds the size (in bytes minus one) and base address used by `lidt`.

## Gate Flags We Use

The header defines the common flag masks:

- `IDT_FLAG_PRESENT` (0x80) — must be set for live entries.
- `IDT_FLAG_RING0` / `IDT_FLAG_RING3` — descriptor privilege level (ring 0 vs user-callable gate).
- `IDT_FLAG_INTERRUPT_32` (0x0E) — 32-bit interrupt gate, automatically clears IF on entry.
- `IDT_FLAG_TRAP_32` (0x0F) — 32-bit trap gate, leaves IF untouched.

A typical kernel gate will combine `IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_INTERRUPT_32`.

## Initialization Flow

The C helper [c_files/src/idt.c](c_files/src/idt.c) currently wires the minimal scaffolding:

1. Zero all 256 entries so undefined vectors fault cleanly instead of jumping into random memory.
2. Prepare the IDT pointer (`limit = sizeof(entries) - 1`, `base = &idt[0]`).
3. Call the assembly helper `idt_load` in [asm/idt.s](asm/idt.s) to execute `lidt`.

At this stage no handlers are installed; later steps will populate entries with real ISR/IRQ stubs before enabling interrupts.

## CPU and Stack Snapshots

The header defines `struct cpu_state` and `struct stack_state` to mirror the register and stack layout pushed by the common interrupt stubs we will add later. These structures keep the C-level dispatch code aligned with the assembly prologue/epilogue and make it obvious which values are preserved across interrupts.

## Next Steps

- Add per-vector stubs (interrupt and IRQ) and fill the table via `idt_set_gate`.
- Remap the PIC and mask interrupts to match the table layout.
- Introduce real handlers (e.g., keyboard) and a safe default stub.
