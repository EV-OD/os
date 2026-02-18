# Interrupt Setup Cheatsheet

Single-topic quick reference for bringing up interrupts in this kernel.

1) **GDT**: Call `gdt_init()` early to load selectors (0x08 code, 0x10 data).
2) **IDT**: Call `idt_init()` to zero 256 entries and load IDTR.
3) **ISRs/IRQs**: Call `isr_install()` to populate vectors 0–31 (CPU) and 32–47 (PIC IRQs).
4) **PIC remap**: `pic_remap(0x20, 0x28)` to move hardware IRQs away from CPU exceptions.
5) **Unmask needed IRQs**: e.g., `pic_clear_mask(1)` for keyboard.
6) **Register handlers**: `register_interrupt_handler(33, keyboard_isr);` etc. Timer stub is pre-registered.
7) **Enable devices**: Initialize drivers (e.g., `keyboard_init()` handles registration + unmask).
8) **Enable interrupts**: Call `interrupts_enable()` when ready; ensure stack/segments are valid.
9) **Acknowledge**: Let the common dispatcher or handler call `pic_acknowledge()` for PIC IRQs.

Gotchas:
- If you see "Unhandled interrupt: 32", you likely lack a handler for IRQ0 or didn’t remap/unmask correctly.
- Always remap before unmasking; otherwise, hardware IRQs may collide with CPU exceptions.
- Keep `cpu_state` / `stack_state` structs in sync with the assembly stub layout.
