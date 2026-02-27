# RandomOS

An experimental 32-bit operating system kernel written in C and x86 Assembly, booted via GRUB (Multiboot). The project includes a higher-half kernel with paging, interrupt handling, device drivers, a freestanding C standard library subset, a structured logging system, and a native compiler (`rosc`) that compiles and executes code at runtime inside the kernel.

## Project Structure

```
├── asm/              Assembly sources (boot, GDT, IDT, ISR stubs, I/O ports)
├── c_files/
│   ├── src/          Kernel C sources (drivers, stdlib, init, logging)
│   └── includes/     Header files
├── compiler/
│   ├── src/          rosc compiler sources (lexer, parser, AST, codegen)
│   ├── include/      Shared compiler types (common.h)
│   └── tests/        Sample .ros programs
├── serial-monitor/   React + TypeScript serial output viewer
├── iso/              ISO layout (GRUB config, boot modules)
├── linker/           Linker script (higher-half, link.ld)
├── docs/             Design documentation
├── cheatsheat/       Quick-reference cheatsheets
└── build/            Build artifacts (generated)
```

## Prerequisites

To build and run this project, you need the following tools installed:
- GCC (`gcc` with `-m32` support) or a Cross-compiler
- NASM (Netwide Assembler)
- CMake
- Make or Ninja
- `genisoimage` (for creating the ISO)
- Bochs or QEMU (for running the OS)

## Building

This project uses CMake. To build the kernel and the ISO image:

1. Create a build directory (if you haven't already):
   ```bash
   mkdir -p build
   cd build
   ```

2. Configure the project:
   ```bash
   cmake .. -G Ninja # Or just cmake .. if using Make
   ```

3. Build:
   ```bash
   ninja # Or make
   ```
   This will compile the assembly and C files, link them into `kernel.elf`, and generate `os.iso`.

## Running

You can run the generated ISO image using an emulator.

From the build directory you can also use the CMake target to boot directly in QEMU:
```bash
ninja run
```
This starts `qemu-system-i386` with `os.iso` and captures serial output to `com1.out`.

### Using Bochs
If you have Bochs installed and configured with `bochsrc.txt`:
```bash
bochs -f bochsrc.txt
```

### Using QEMU
```bash
qemu-system-i386 -cdrom build/os.iso
```

## Current Progress

### Architecture

- **Higher-half kernel** — kernel linked at 0xC0100000, running above 3 GB in virtual address space. GRUB loads at 1 MB physical; `loader.s` sets up 4 MB PSE paging, jumps to the higher half, and removes the identity map.
- **Protected-mode GDT** — 3-entry table (null, kernel code 0x08, kernel data 0x10) loaded with `lgdt` and a far-jump CS reload.
- **IDT & ISR plumbing** — 256-entry IDT; assembly stubs for CPU exceptions (0–31) and PIC IRQs (32–47) with a uniform `pusha`-based frame dispatching into a C handler table.
- **PIC remap** — 8259 master/slave remapped to 0x20/0x28 to avoid collision with CPU exception vectors; mask/unmask helpers for individual IRQ lines.
- **Multiboot module loading** — GRUB-loaded flat binary modules are relocated to the higher-half virtual address and executed in-place.

### Drivers

- **Keyboard** — PS/2 scan code set 1 interrupt handler (IRQ1) with a 128-byte ring buffer; non-blocking and blocking read APIs feed into `getchar`/`readline`/`scanf`.
- **Serial (COM1)** — configurable baud rate (default 9600), 8N1 line format, FIFO enabled, polled transmit. Used for kernel log output.

### Kernel Libraries

- **stdio** — VGA text-mode framebuffer console (80×25) with `putchar`, `puts`, `write`, `getchar`, `readline`, `scanf`; cursor control and screen clear. Assembly `outb`/`inb` for port I/O.
- **string** — `strlen`, `strcpy`, `strncpy`, `strcat`, `strncat`, `strcmp`, `strncmp`, `memset`, `memcpy`, `memcmp`, `atoi`, `itoa`, `sprintf` (supports `%c`, `%d`, `%x`, `%s`, `%%`).
- **logging** — multi-target output (framebuffer, serial, or both) with leveled helpers (`log_debug`, `log_info`, `log_warning`, `log_error`) that prepend `[LEVEL]` tags parseable by the serial monitor.

### rosc Compiler (Phase 1)

A native compiler for the **RandomOS Language** (.ros), built directly into the kernel (no file system yet). Phase 1 supports:

- **Lexer** — tokenises integer literals, arithmetic operators (`+ - * /`), parentheses, `let` bindings, type annotations (`i32`, `u32`, `bool`), identifiers, and line comments (`//`).
- **Parser** — recursive-descent parser producing an AST with correct operator precedence (multiplicative before additive).
- **Code generator** — constant-folds all expressions at compile time, emits a minimal x86-32 flat binary (`mov eax, <result>; ret`), and executes it in-place via function-pointer cast.
- **Interactive demo** — on boot, `rosc_run()` walks through each compiler stage (source → tokens → AST → codegen → execution), pausing for a keypress between stages.

### Serial Monitor (Web UI)

A React + TypeScript + Vite application in `serial-monitor/` that connects to a backend SSE endpoint streaming COM1 output. Features include real-time color-coded log display, level filtering, text/regex search, pause/resume, auto-scroll, timestamps, log download, keyboard shortcuts, and auto-reconnect.

## Documentation

Detailed docs live alongside the source:

### Architecture (`docs/architecture/`)
- [gdt.md](docs/architecture/gdt.md) — Global Descriptor Table design, access/granularity bytes, current entries.
- [idt.md](docs/architecture/idt.md) — Interrupt Descriptor Table layout, gate flags, init flow.
- [isr.md](docs/architecture/isr.md) — ISR/IRQ stubs, stack frame layout, C dispatcher, handler registration.
- [paging.md](docs/architecture/paging.md) — Higher-half paging setup, 4 MB PSE pages, page directory, runtime helpers.
- [multiboot.md](docs/architecture/multiboot.md) — Multiboot module loading, GRUB handoff, virtual address translation.

### Kernel (`docs/kernel/`)
- [boot_sequence.md](docs/kernel/boot_sequence.md) — Full boot flow from GRUB to user-facing output, init order dependencies.
- [logging.md](docs/kernel/logging.md) — Log devices, levels, printf-style API, serial monitor integration.
- [stdio.md](docs/kernel/stdio.md) — Framebuffer console, cursor control, input functions (getchar, readline, scanf).
- [string.md](docs/kernel/string.md) — String, memory, and conversion functions (sprintf, itoa, atoi).

### Drivers (`docs/drivers/`)
- [keyboard.md](docs/drivers/keyboard.md) — PS/2 interrupt handler, scancode mapping, ring buffer.
- [pic.md](docs/drivers/pic.md) — 8259 PIC remap, EOI, mask control.
- [serial.md](docs/drivers/serial.md) — COM1 line control, FIFO, modem, baud rate.

### Display (`docs/display/`)
- [framebuffer.md](docs/display/framebuffer.md) — VGA text-mode cell format, color palette, cursor ports.

### Compiler (`compiler/`)
- [compiler_features.md](compiler/compiler_features.md) — Language design, type system, syscall interface, phase plan.

### Cheatsheets (`cheatsheat/`)
- Quick-reference sheets for [IDT gates](cheatsheat/cpu/idt/idt_gate_flags_cheatsheet.md), [PIC commands](cheatsheat/hardware/pic/pic_command_word_cheatsheet.md), [serial line control](cheatsheat/io/serial/serial_line_control_cheatsheet.md), [framebuffer ports](cheatsheat/display/framebuffer/framebuffer_port_format_cheatsheet.md), and the [interrupt setup recipe](cheatsheat/interrupts/interrupt_setup_cheatsheet.md).

## License

This project is for educational and experimental purposes.
