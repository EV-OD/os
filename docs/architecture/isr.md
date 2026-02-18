# Interrupt Service Routines (ISR) and IRQ Stubs

This note documents how interrupts are dispatched into C code. Stubs live in [asm/isr.s](../../asm/isr.s), registration and dispatch logic in [c_files/src/isr.c](../../c_files/src/isr.c), with the public API in [c_files/includes/isr.h](../../c_files/includes/isr.h).

## Stack and Register Layout

Each stub pushes (or receives) an error code and the interrupt number, then jumps to `common_isr_stub` which executes `pusha`. `pusha` saves registers in this order: eax, ecx, edx, ebx, esp (original), ebp, esi, edi. The structures in [c_files/includes/idt.h](../../c_files/includes/idt.h) mirror that order so the C handler can read them safely.

After `pusha`, the common stub passes three arguments to C: `cpu_state*`, `stack_state*` (points to error code, eip, cs, eflags), and the interrupt number.

## Gate Setup

`isr_install()` programs the IDT entries for:
- CPU exceptions 0–31 via `isr0` … `isr31`.
- Remapped PIC IRQs 32–47 via `irq0` … `irq15` (assuming PIC remap to 0x20/0x28).

All gates use a 32-bit interrupt gate (present, ring 0).

## C Dispatcher

`interrupt_handler(cpu, stack, interrupt)` is the common entry point called from assembly. Behavior:
- If a handler was registered with `register_interrupt_handler`, it is invoked.
- If none is registered, a fallback message prints via the serial port.
- For IRQ vectors in the PIC range, `pic_acknowledge` is called to send EOI so more interrupts can arrive.

## Adding Handlers

1. Register a handler from C: `register_interrupt_handler(vector, my_handler);`
2. Ensure the handler acknowledges or relies on the common dispatcher’s EOI for PIC IRQs.
3. Initialize the system in order: `gdt_init(); idt_init(); isr_install(); pic_remap(...);` then enable interrupts (`sti`) when ready.
