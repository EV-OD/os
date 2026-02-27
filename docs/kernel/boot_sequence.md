# Kernel Boot Sequence

This document describes the full initialization flow from GRUB handoff to the first user-facing output.

## Boot Flow

### Phase 1: Assembly Bootstrap (loader.s)

1. **GRUB loads the kernel** at the 1 MB physical mark and jumps to the `loader` entry point, passing:
   - `eax` = 0x2BADB002 (Multiboot magic)
   - `ebx` = physical address of the Multiboot info structure

2. **Save Multiboot registers** into `esi` (magic) and `edi` (info pointer) before they are clobbered.

3. **Enable higher-half paging**:
   - Load page directory physical address into CR3
   - Set CR4.PSE for 4 MB pages
   - Set CR0.PG to enable paging
   - Jump to `higher_half` (virtual address 0xC01XXXXX)

4. **Remove identity map**: clear PDE[0] and flush TLB.

5. **Switch to kernel stack** (16 KB in `.bss`).

6. **Call `kmain(eax, ebx)`** with the Multiboot magic and the virtual-adjusted info pointer.

### Phase 2: Kernel Initialization (kernel_init.c)

`kmain()` calls `kernel_init(mb_magic, mb_info)` which sets up the system in this exact order:

| Step | Function | Purpose |
|------|----------|---------|
| 1 | `gdt_init()` | Load the Global Descriptor Table (null + kernel code + kernel data + user code + user data + TSS placeholder) |
| 2 | `idt_init()` | Zero 256 IDT entries and load the IDTR |
| 3 | `isr_install()` | Register ISR stubs for exceptions (0–31) and IRQs (32–47) in the IDT; pre-register a timer stub on vector 32 |
| 4 | `pic_remap(0x20, 0x28)` | Reprogram the 8259 PIC to map IRQ 0–7 to vectors 0x20–0x27 and IRQ 8–15 to vectors 0x28–0x2F |
| 5 | `keyboard_init()` | Register the keyboard ISR on vector 33 (IRQ1) and unmask IRQ1 |
| 6 | `serial_begin(9600)` | Configure COM1 at 9600 baud: set divisor, 8N1 line format, enable FIFO, set modem control |
| 7 | `paging_init()` | Log the paging status (CR3, PSE, PDE verification) |
| 8 | `pfa_init(mb, phys_start, phys_end)` | Parse the Multiboot mmap, free available frames, reserve first 1 MB + kernel image |
| 9 | `kheap_init()` | Install a single all-free block in `[0xC0300000, 0xC03FF000)` |
| 10 | `tss_init()` | Write the TSS descriptor into GDT slot 5 (base = &tss, limit = sizeof(tss)−1) and execute `ltr 0x28` to load the Task Register |
| 11 | `pit_init(PIT_TICK_MS)` | Program the 8254 PIT channel 0 to fire IRQ0 every 10 ms (100 Hz, divider = 11931) |
| 12 | `pic_clear_mask(0)` | Unmask IRQ0 in the PIC so timer interrupts are delivered (all other IRQs remain masked until explicitly unmasked) |
| 13 | `interrupts_enable()` | Execute `sti` to start handling hardware interrupts |

### Phase 3: Application Entry (kmain.c)

After `kernel_init()` returns:

1. `fb_clear()` — clear the VGA framebuffer.
2. `cursor_move_home()` — reset the cursor to position (0,0).
3. `display_boot_info()` — print the kernel version string to both serial and framebuffer.
4. Run self-tests (`kheap_test`, `pfa_test`).
5. `process_init()` / `sched_init()` — reset PID counter and zero the CFS run queue.
6. `process_create()` × 4 — build process descriptors for `task_a` (nice=−5), `task_b` (nice=0), `task_c` (nice=+5), `task_idle` (nice=+19), each with a 4 KB kernel stack and a fake interrupt-return frame.
7. `sched_add()` × 4 — insert all four processes into the CFS run queue sorted by vruntime.
8. `interrupts_disable()` — execute `cli` to prevent a timer tick arriving mid-switch.
9. `sched_start()` — dequeue the minimum-vruntime process, load its stack, and `iret` into it. **Never returns.**

## Initialization Order Dependencies

The order in `kernel_init()` is critical:

- **GDT before IDT**: the IDT entries reference the GDT code selector (0x08).
- **IDT before ISR install**: ISR install calls `idt_set_gate` to write IDT entries.
- **ISR install before PIC remap**: the handlers must be in place before interrupts arrive at the remapped vectors.
- **PIC remap before keyboard init**: keyboard init unmasks IRQ1, which requires the PIC to already be remapped.
- **Serial before paging init**: paging_init logs diagnostics to serial.
- **kheap before tss_init**: `tss_init` does not allocate heap by itself but `process_create` relies on the heap, and tss init is grouped with the multitasking subsystem setup.
- **tss_init before pit_init**: the TSS must be loaded (`ltr`) before the timer fires and triggers the first context switch, which needs a valid `esp0`.
- **pit_init + pic_clear_mask before `sti`**: the timer and its PIC mask must be configured before `sti` enables delivery.
- **Everything before `sti`**: all handlers and hardware must be configured before interrupts are enabled.

In `kmain()`, `process_init`/`sched_init`/`sched_start` run **after** `kernel_init` returns (which includes `sti`). The `interrupts_disable()` call in `kmain` before `sched_start` re-disables interrupts just for the atomic first-task launch; `iret` restores `EFLAGS.IF=1` from the first task's saved flags (`0x0202`).

## Helper Inlines (interrupts.h)

```c
static inline void interrupts_enable(void);   // sti
static inline void interrupts_disable(void);  // cli
static inline void cpu_halt(void);            // hlt
```

## Related Documents

- [docs/kernel/multitasking.md](multitasking.md) — Full multitasking initialization and context-switch walkthrough.
- [docs/architecture/tss.md](../architecture/tss.md) — TSS structure and `esp0` details.
- [docs/drivers/pit.md](../drivers/pit.md) — PIT timer configuration.
- [docs/kernel/process.md](process.md) — Process descriptor and initial stack frame.
- [docs/kernel/sched.md](sched.md) — CFS scheduler and `sched_start()`.
