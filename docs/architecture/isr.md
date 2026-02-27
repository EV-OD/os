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

`interrupt_handler(cpu, stack, interrupt)` is the common entry point called from assembly.

Return type is `unsigned int`. The return value carries the **new kernel ESP** when a context switch should take place, or `0` when execution may continue on the same stack.

Behavior:
- If a handler was registered with `register_interrupt_handler`, it is invoked.
- For vector 32 (PIT IRQ0 / timer): sends EOI via `pic_acknowledge` **first**, then calls `sched_tick(cpu, 32)` and returns its result (new ESP or 0). EOI is sent before the switch so PIC can accept the next timer tick even if the current process never returns to `interrupt_handler`.
- For other IRQ vectors in the PIC range, `pic_acknowledge` is called last.
- If no handler is registered for a non-timer interrupt, a fallback message prints via the serial port.

## Context-Switch Path in `common_isr_stub`

After `call interrupt_handler; add esp, 12`, the stub checks the return value:

```nasm
    test eax, eax         ; was a context switch requested?
    jz   .no_ctx_switch
    mov  esp, eax         ; ← load new process's kernel stack pointer
.no_ctx_switch:
    popa                  ; restore GP registers (from whichever stack is now active)
    add  esp, 8           ; skip dummy error_code + interrupt_number
    iret                  ; pop EIP, CS, EFLAGS → resume process
```

If `eax != 0`, the kernel stack is atomically replaced before `popa`. From that point on the CPU is running on the **next process's** kernel stack, restoring its saved registers and returning to its saved EIP via `iret`.

This mechanism means a context switch costs only:
- One `test`/`jz` in the common stub (always executed).
- One `mov esp, eax` (only when switching).
- No extra assembly entry points per task.

### EOI Ordering Rationale

```
interrupt_handler():
    1. call registered handler (e.g. timer counter update)
    2. pic_acknowledge(32)      ← EOI sent here for IRQ0
    3. sched_tick(cpu, 32)      ← may not return to same stack
    return new_esp
```

EOI is sent **before** `sched_tick` so that if the context switch lands on a task that re-enables interrupts quickly, the PIC is already ready to fire the next timer tick. Delaying EOI until after the switch would suppress timer interrupts for the duration of the new task's first run after returning from the switch.

## Adding Handlers

1. Register a handler from C: `register_interrupt_handler(vector, my_handler);`
2. Ensure the handler acknowledges or relies on the common dispatcher's EOI for PIC IRQs.
3. Initialize the system in order: `gdt_init(); idt_init(); isr_install(); pic_remap(...);` then enable interrupts (`sti`) when ready.

## Related Documents

- [docs/kernel/multitasking.md](../kernel/multitasking.md) — Full context-switch walkthrough.
- [docs/kernel/sched.md](../kernel/sched.md) — `sched_tick()` implementation and return value semantics.
- [docs/drivers/pit.md](../drivers/pit.md) — PIT IRQ0 source.
