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

`kmain()` calls `kernel_init()` which sets up the system in this exact order:

| Step | Function | Purpose |
|------|----------|---------|
| 1 | `gdt_init()` | Load the Global Descriptor Table (null + kernel code + kernel data) |
| 2 | `idt_init()` | Zero 256 IDT entries and load the IDTR |
| 3 | `isr_install()` | Register ISR stubs for exceptions (0–31) and IRQs (32–47) in the IDT; pre-register a timer stub on vector 32 |
| 4 | `pic_remap(0x20, 0x28)` | Reprogram the 8259 PIC to map IRQ 0–7 to vectors 0x20–0x27 and IRQ 8–15 to vectors 0x28–0x2F |
| 5 | `keyboard_init()` | Register the keyboard ISR on vector 33 (IRQ1) and unmask IRQ1 |
| 6 | `serial_begin(9600)` | Configure COM1 at 9600 baud: set divisor, 8N1 line format, enable FIFO, set modem control |
| 7 | `paging_init()` | Log the paging status (CR3, PSE, PDE verification) |
| 8 | `interrupts_enable()` | Execute `sti` to start handling hardware interrupts |

### Phase 3: Application Entry (kmain.c)

After `kernel_init()` returns:

1. `fb_clear()` — clear the VGA framebuffer.
2. `cursor_move_home()` — reset the cursor to position (0,0).
3. `display_boot_info()` — print the kernel version string to both serial and framebuffer.
4. `rosc_run()` — launch the rosc compiler demo (Phase 1: compile and execute a hardcoded arithmetic program).
5. Enter an infinite `while (1) {}` halt loop.

## Initialization Order Dependencies

The order in `kernel_init()` is critical:

- **GDT before IDT**: the IDT entries reference the GDT code selector (0x08).
- **IDT before ISR install**: ISR install calls `idt_set_gate` to write IDT entries.
- **ISR install before PIC remap**: the handlers must be in place before interrupts arrive at the remapped vectors.
- **PIC remap before keyboard init**: keyboard init unmasks IRQ1, which requires the PIC to already be remapped.
- **Serial before paging init**: paging_init logs diagnostics to serial.
- **Everything before `sti`**: all handlers and hardware must be configured before interrupts are enabled.

## Helper Inlines (interrupts.h)

```c
static inline void interrupts_enable(void);   // sti
static inline void interrupts_disable(void);  // cli
static inline void cpu_halt(void);            // hlt
```
