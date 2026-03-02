# RandomOS

An experimental 32-bit operating system kernel written in C and x86 Assembly, booted via GRUB (Multiboot). The project features a higher-half kernel with paging, interrupt handling, device drivers, a freestanding C standard library subset, a structured logging system, a FAT32 virtual filesystem, a CFS multitasking scheduler, a VESA GUI subsystem, and a fully native compiler (`rosc`) for the RandomOS Language (`.ros`) that produces ring-3 user-mode executables at runtime inside the OS.

See [os_features.md](os_features.md) for a full abstract overview of every major feature.

## Project Structure

```
├── asm/              Assembly sources (boot, GDT, IDT, ISR stubs, I/O ports, user programs)
├── c_files/
│   ├── src/          Kernel C sources (drivers, stdlib, scheduler, VFS, GUI, shell, compiler)
│   └── includes/     Header files
├── compiler/
│   ├── src/          rosc compiler sources (lexer, parser, AST, codegen, driver)
│   ├── include/      Shared compiler types (common.h)
│   └── tests/        Sample .ros programs
├── serial-monitor/   React + TypeScript serial output viewer (Vite)
├── iso/              ISO layout (GRUB config, boot modules)
├── linker/           Linker script (higher-half, link.ld)
├── docs/             Design documentation
├── cheatsheat/       Quick-reference cheatsheets
├── os_features.md    Full abstract feature overview
└── build/            Build artifacts (generated)
```

## Prerequisites

To build and run this project, you need the following tools installed:
- GCC (`gcc` with `-m32` support) or a cross-compiler targeting `i686-elf`
- NASM (Netwide Assembler)
- CMake ≥ 3.16
- Make or Ninja
- `genisoimage` (for creating the bootable ISO)
- Bochs or QEMU (for emulation)

Optional for GUI mode:
- A VESA-capable GRUB / emulator (QEMU supports VESA out of the box)

## Building

This project uses CMake. Two build configurations are available:

### Text Shell Mode (default)
```bash
mkdir -p build && cd build
cmake .. -G Ninja
ninja
```

### GUI / Framebuffer Mode
```bash
mkdir -p build && cd build
cmake .. -G Ninja -DGUI_MODE=ON
ninja
```
Both produce `kernel.elf` and `os.iso` in the build directory.

## Running

### QEMU (recommended)
```bash
ninja run          # from build/ — launches qemu-system-i386, serial → com1.out
```
Or directly:
```bash
qemu-system-i386 -cdrom build/os.iso -serial file:com1.out
```

### QEMU with GUI (framebuffer)
```bash
qemu-system-i386 -cdrom build/os.iso -vga std
```

### Bochs
```bash
bochs -f bochsrc.txt
```

## Current Features

### Architecture

- **Higher-half kernel** — linked at `0xC0100000` (3 GB virtual). GRUB loads the kernel at 1 MB physical; `loader.s` installs a 4 MB PSE page, jumps to the higher half, and tears down the identity map.
- **Protected-mode GDT** — 5-entry table: null, kernel code/data (`0x08`/`0x10`), user code/data (`0x18`/`0x20`), TSS. Loaded with `lgdt` and a far-jump CS reload.
- **IDT & ISR plumbing** — 256-entry IDT; assembly stubs for CPU exceptions (0–31) and PIC IRQs (32–47) with a uniform `pusha`-based frame dispatching into a C handler table.
- **PIC remap** — 8259 master/slave remapped to `0x20`/`0x28`; per-IRQ mask/unmask helpers.
- **Physical Frame Allocator (PFA)** — bitmap-based allocator tracking all 4 KB physical frames; `pfa_alloc_frame()` / `pfa_free_frame()`.
- **Kernel Heap** — boundary-tag allocator (`kmalloc` / `kfree`) over a fixed `~1 MB` virtual region (`0xC0300000–0xC03FF000`); split and coalesce on every alloc/free.
- **TSS** — single kernel TSS for ring-0 stack pointer on interrupt entry from ring 3. Updated by the scheduler on every context switch.
- **Multiboot module loading** — GRUB-loaded flat binary modules relocated to the higher-half virtual address and executed in-place.

### Multitasking & Processes

- **CFS Scheduler** — Completely Fair Scheduler with virtual runtime (`vruntime`) and a fixed 10 ms PIT tick. Picks the task with lowest `vruntime` on every tick; `sched_add` / `sched_remove` for dynamic task management.
- **Context Switch** — `common_isr_stub` saves/restores the full `pusha` register frame; `sched_tick()` returns the new ESP; `iret` resumes the next process. CR3 is switched for user processes.
- **Ring-3 User Processes** — `process_create_user()` builds a fresh page directory, maps code at `0x08048000` and a stack at `0xBFFFF000`, then constructs a ring-3 `iret` frame. Processes call `int 0x80` for syscalls.
- **Syscall Dispatcher** — `int 0x80` handler dispatching `SYS_EXIT`, `SYS_WRITE`, `SYS_READ`, `SYS_GETPID`, `SYS_YIELD`, and GUI syscalls.

### Filesystem

- **ATA PIO driver** — primary master IDENTIFY + 28-bit LBA read/write via port I/O (`0x1F0–0x1F7`).
- **FAT32 driver** — full BPB/EBPB parse, cluster chain traversal, directory enumeration, file read/write, `mkdir`, `unlink`. Supports MBR partition tables and super-floppy images.
- **VFS layer** — POSIX-like fd table (`open`, `read`, `write`, `seek`, `close`, `stat`, `readdir`, `mkdir`, `unlink`) sitting above the FAT32 driver. Up to 16 simultaneous open files.

### Drivers

- **Keyboard** — PS/2 scan code set 1 (IRQ1), 128-byte ring buffer, non-blocking + blocking read, feeds `getchar` / `readline` / `scanf`.
- **Serial (COM1)** — 9600 baud, 8N1, FIFO enabled, polled TX. All kernel log output goes here.
- **ATA** — polled PIO, primary channel, master drive.
- **PIT** — channel 0 at ~100 Hz (10 ms tick); drives the scheduler and `pit_sleep_ms()`.
- **PS/2 Mouse** — 3-button protocol (IRQ12), relative to absolute coordinate tracking (GUI mode).

### Kernel Libraries

- **stdio** — VGA text-mode console (80×25): `putchar`, `puts`, `puts_color`, `write`, `getchar`, `readline`, `scanf`; hardware cursor control; screen clear.
- **string** — `strlen`, `strcpy`, `strncpy`, `strcat`, `strncat`, `strcmp`, `strncmp`, `memset`, `memcpy`, `memcmp`, `atoi`, `itoa`, `sprintf` (`%c`, `%d`, `%x`, `%s`, `%%`).
- **logging** — multi-target (`framebuffer`, `serial`, or both); leveled output: `log_debug`, `log_info`, `log_warning`, `log_error`; `[LEVEL]` tags recognised by the serial monitor.
- **kheap** — `kmalloc` / `kfree` / `krealloc` with boundary-tag coalescing.
- **textbuf** — line-based text buffer with cursor tracking; used by the `rxt` editor and `tbuf_*` syscalls.

### Shell (rosh)

An interactive command-line shell running as a kernel task. Features:

- Line editor with backspace and coloured prompt
- Tokeniser splitting on whitespace into `argc`/`argv`
- **Command resolution**: `./path.rox` → `/path.rox` → `/bin/<cmd>.rox` → built-in table
- **OS directory layout** created at boot: `/bin`, `/etc`, `/home`, `/tmp`, `/var/log`, `/lib/ros`
- **Standard library** written to `/lib/ros/` at boot: `gui.ros`, `math.ros`, `io.ros`
- **Two modes**: `nerd` (text shell, default) and `gui` (graphical desktop, compile-time opt-in)

| Command | Description |
|---------|-------------|
| `help` | List all commands |
| `ls [-a] [path]` | List directory contents |
| `cd [path]` | Change directory |
| `cat <file>` | Print file contents |
| `echo <text>` | Print text |
| `clear` | Clear screen |
| `mkdir <dir>` | Create directory |
| `touch <file>` | Create empty file |
| `rm <file>` | Remove file |
| `pwd` | Print working directory |
| `stat <path>` | Show file/dir metadata |
| `write <file> <text>` | Write text to file |
| `uname` | System info |
| `whoami` | Current user |
| `mode [nerd\|gui]` | Show/set OS mode |
| `rosc [-f] <src.ros> [out.rox]` | Compile a `.ros` source file |
| `sample [name\|list]` | Generate a sample `.ros` program |
| `setup` | Compile & install `rxt` editor and `term` to `/bin` |
| `desktop add <name\|path.rox>` | Add a desktop icon |
| `desktop delete <label>` | Remove a desktop icon |
| `microui` | Launch microui GUI demo (GUI mode) |

### rosc Compiler & RandomOS Language (.ros)

A native compiler built into the kernel that compiles `.ros` source files to `.rox` user-mode executables at runtime. Full pipeline: **Lexer → Parser → AST → Codegen → .rox binary**.

**Language features (fully implemented):**
- Types: `i32`, `u32`, `bool`, `str`
- Variables: `let` (immutable), `let mut` (mutable)
- Arithmetic: `+ - * / %`, bitwise `& | ^ ~ << >>`, logical `&& || !`
- Compound assignment: `+= -= *= /= %=`
- Control flow: `if / else if / else`, `while`, `for x in lo..hi`, `break`, `continue`
- Functions: `fn name(args) -> ret { }`, recursion, nested calls
- String literals with escape sequences (`\n`, `\t`, `\\`, `\"`)
- Built-ins: `print()`, `println()`, `getarg()`
- Import system: `import gui`, `import math`, `import io` (loads from `/lib/ros/`)
- GUI syscalls: `gui_window`, `gui_fill`, `gui_text`, `gui_flush`, `gui_wait`, `gui_poll`, `gui_mouse`, `gui_close`, and dozens of widget helpers from `gui.ros`

**Standard library (`/lib/ros/`):**
| Library | File | Contents |
|---------|------|----------|
| GUI | `gui.ros` | Colors, layout helpers, buttons, checkboxes, sliders, radio, progress, textbox, tooltip, notify, tags, stat cards |
| Math | `math.ros` | `abs`, `min`, `max`, `clamp`, `isqrt`, `ipow`, `lerp`, `map_range` |
| I/O | `io.ros` | `io_open/close/save`, `io_getline`, `io_key`, `io_lines`, `io_cursor`, header/separator helpers |

**Sample programs** (generate with `sample <name>`):
`hello`, `strings`, `math`, `fib`, `loops`, `funcs`, `gui`, `mu`, `test`

### GUI Mode (VESA Framebuffer)

Enabled at compile time with `-DGUI_MODE=ON`. Provides a full pixel-level graphical desktop:

- **Framebuffer** — VESA linear framebuffer via GRUB, double-buffered (`back → front` blit on flush), `put_pixel`, `flush_rect`.
- **Font** — embedded 256-glyph 8×16 VGA bitmap font.
- **GFX** — rectangles, filled rects, lines, circles, filled circles, rounded rects, text rendering.
- **Window Manager** — window list with Z-order, focus, title bar, drag, close button, event dispatch.
- **Desktop** — taskbar, wallpaper, icon grid; `desktop add/delete` manage icons at runtime; config persisted to `/etc/desktop.conf`.
- **GUI Terminal** — `terminal_t` vtable backed by the framebuffer; shell and `.rox` processes work in both modes unchanged.
- **Mouse** — PS/2 three-button protocol (IRQ12), absolute cursor with sprite rendering.

### Serial Monitor (Web UI)

A React + TypeScript + Vite application in `serial-monitor/` that streams COM1 output. Features:
- Real-time colour-coded log display with `[DEBUG]` / `[INFO]` / `[WARNING]` / `[ERROR]` level filtering
- Text and regex search, pause/resume, auto-scroll, timestamps
- Log download, keyboard shortcuts, auto-reconnect

## Documentation

### Overview
- [os_features.md](os_features.md) — Full abstract OS feature overview (start here)

### Architecture (`docs/architecture/`)
- [gdt.md](docs/architecture/gdt.md) — GDT design, access/granularity bytes, 5-entry layout.
- [idt.md](docs/architecture/idt.md) — IDT layout, gate flags, init flow.
- [isr.md](docs/architecture/isr.md) — ISR/IRQ stubs, stack frame, C dispatcher, handler registration.
- [paging.md](docs/architecture/paging.md) — Higher-half paging, 4 MB PSE pages, runtime helpers.
- [pfa.md](docs/architecture/pfa.md) — Physical frame allocator, bitmap layout.
- [tss.md](docs/architecture/tss.md) — TSS setup, ring-0 stack, scheduler update.
- [multiboot.md](docs/architecture/multiboot.md) — Multiboot module loading, GRUB handoff.

### Kernel (`docs/kernel/`)
- [boot_sequence.md](docs/kernel/boot_sequence.md) — Full boot flow, init order dependencies.
- [shell.md](docs/kernel/shell.md) — rosh shell, commands, built-in table, stdlib, rxt editor.
- [multitasking.md](docs/kernel/multitasking.md) — PIT, TSS, CFS scheduler, context-switch walkthrough.
- [process.md](docs/kernel/process.md) — User-mode process creation, page directory, ring-3 iret frame.
- [sched.md](docs/kernel/sched.md) — CFS vruntime, run queue, `sched_tick`, `sched_add/remove`.
- [kheap.md](docs/kernel/kheap.md) — Boundary-tag allocator, block layout, split/coalesce.
- [vfs.md](docs/kernel/vfs.md) — VFS fd table, API reference, open flags.
- [logging.md](docs/kernel/logging.md) — Log devices, levels, serial monitor integration.
- [stdio.md](docs/kernel/stdio.md) — Framebuffer console, cursor control, input functions.
- [string.md](docs/kernel/string.md) — String, memory, and conversion functions.

### Drivers (`docs/drivers/`)
- [keyboard.md](docs/drivers/keyboard.md) — PS/2 IRQ1 handler, scancode mapping, ring buffer.
- [pic.md](docs/drivers/pic.md) — 8259 PIC remap, EOI, mask control.
- [serial.md](docs/drivers/serial.md) — COM1 line control, FIFO, baud rate.
- [pit.md](docs/drivers/pit.md) — PIT channel 0, frequency, sleep helper.
- [ata.md](docs/drivers/ata.md) — ATA PIO driver, IDENTIFY, LBA read/write.
- [fat32.md](docs/drivers/fat32.md) — FAT32 BPB parse, cluster chains, dir enumeration.

### GUI (`docs/gui/`)
- [overview.md](docs/gui/overview.md) — Layer diagram, enable instructions, nerd vs GUI mode.
- [architecture.md](docs/gui/architecture.md) — Data structures, module dependency order, event model.
- [framebuffer.md](docs/gui/framebuffer.md) — fb_t layout, double buffering, put_pixel, flush.
- [window_manager.md](docs/gui/window_manager.md) — Window list, Z-order, focus, drag, events.
- [font_rendering.md](docs/gui/font_rendering.md) — 8×16 glyph bitmap, font_draw_char/str.
- [mouse.md](docs/gui/mouse.md) — PS/2 mouse protocol, IRQ12, cursor rendering.

### Compiler (`compiler/`)
- [compiler_features.md](compiler/compiler_features.md) — Full language spec, type system, syscall ABI, phase plan.

### Cheatsheets (`cheatsheat/`)
- [interrupt_setup_cheatsheet.md](cheatsheat/interrupts/interrupt_setup_cheatsheet.md) — Step-by-step interrupt bring-up recipe.
- [idt_gate_flags_cheatsheet.md](cheatsheat/cpu/idt/idt_gate_flags_cheatsheet.md) — IDT gate descriptor bit layout.
- [pic_command_word_cheatsheet.md](cheatsheat/hardware/pic/pic_command_word_cheatsheet.md) — 8259 ICW/OCW command words.
- [serial_line_control_cheatsheet.md](cheatsheat/io/serial/serial_line_control_cheatsheet.md) — COM1 UART register map.
- [framebuffer_port_format_cheatsheet.md](cheatsheat/display/framebuffer/framebuffer_port_format_cheatsheet.md) — VGA text-mode cell format.
- [shell_cheatsheet.md](cheatsheat/shell/shell_cheatsheet.md) — rosh built-in commands quick reference.
- [rosc_language_cheatsheet.md](cheatsheat/rosc/rosc_language_cheatsheet.md) — .ros syntax, built-ins, import, GUI functions.
- [gui_cheatsheet.md](cheatsheat/gui/gui_cheatsheet.md) — GUI syscalls, widget API, color constants.

## License

This project is for educational and experimental purposes.
