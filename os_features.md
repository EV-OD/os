# RandomOS — Feature Overview

> This document gives a high-level, abstract description of every major feature in RandomOS.
> For implementation details, follow the links to the relevant doc or source file.

---

## 1. Boot & Initialisation

RandomOS is loaded by GRUB using the **Multiboot** protocol. The boot sequence proceeds in a strict order to satisfy hardware dependencies:

```
GRUB  →  loader.s  →  higher-half jump  →  kernel_init()  →  shell_run() / desktop_run()
```

The assembly loader (`asm/loader.s`) sets up a minimal 4 MB PSE page that maps the kernel into the upper 1 GB of virtual address space (higher-half kernel at `0xC0100000`), then performs a far jump to enter C. From there, `kernel_init()` initialises every subsystem in dependency order and passes control to the interactive shell.

**Key properties:**
- Kernel lives above 3 GB virtual — user programs never collide with kernel addresses
- Identity map torn down immediately after the higher-half jump
- GRUB hands off a `multiboot_info_t` structure with memory map, module list, and (in GUI mode) framebuffer info

---

## 2. Memory Management

### Physical Frame Allocator (PFA)
A **bitmap allocator** that tracks every 4 KB physical frame. At boot it marks all frames as reserved, then marks usable regions free based on the multiboot memory map. Single operations: `pfa_alloc_frame()` (find-first-free + set bit) and `pfa_free_frame()` (clear bit).

### Kernel Heap
A **boundary-tag allocator** (`kmalloc` / `kfree`) over a fixed `~1 MB` virtual region (`0xC0300000–0xC03FF000`). Every block carries a header with magic, size, free flag, and prev/next pointers. Allocation searches for a free block large enough, splits it if the remainder is usable, and coalesces adjacent free blocks on every `kfree`. No page faults needed — the entire heap range is within the initial 4 MB PSE mapping.

### Virtual Memory & Paging
Each user process gets its own **page directory** allocated from the PFA. The kernel's 4 MB PSE page is mirrored into every user page directory so that system calls and interrupts don't need a CR3 switch. User code is mapped at `0x08048000` (4 KB pages) and the stack at `0xBFFFF000`.

---

## 3. CPU Descriptor Tables

### GDT (Global Descriptor Table)
Five entries: null, kernel code (`0x08`), kernel data (`0x10`), user code (`0x18`), user data (`0x20`), plus a TSS descriptor. Loaded with `lgdt` followed by a far jump to flush the segment registers.

### IDT (Interrupt Descriptor Table)
256 entries. CPU exceptions (vectors 0–31) each have a dedicated assembly stub that pushes a dummy error code (where the CPU doesn't push one) and the vector number, then falls into `common_isr_stub`. PIC hardware IRQs use vectors 32–47 after the PIC remap.

### TSS (Task State Segment)
A single TSS holds `esp0` — the ring-0 kernel stack pointer. On every context switch the scheduler updates `esp0` to point at the top of the incoming process's kernel stack, so that an `int 0x80` from ring 3 lands on the right stack.

---

## 4. Interrupt Handling

The interrupt subsystem is a **three-layer pipeline**:

```
Hardware event
    │
    ▼
Assembly stub (asm/isr.s)
    │  pusha  →  push interrupt number  →  call interrupt_handler
    ▼
C dispatcher (c_files/src/isr.c)
    │  calls registered C handler  →  pic_acknowledge  →  sched_tick
    ▼
Handler / Scheduler
```

Any subsystem can register a handler at runtime: `register_interrupt_handler(vector, fn)`. The PIC is remapped so hardware IRQs start at vector 32, avoiding collision with CPU exceptions at 0–31.

---

## 5. Multitasking

### CFS Scheduler
RandomOS implements a **Completely Fair Scheduler** (CFS). Each process has a `vruntime` counter that increments proportionally to its time on CPU. On every PIT tick (~10 ms) the scheduler picks the ready process with the lowest `vruntime` and switches to it.

### Context Switch
Context switching is done entirely in the `common_isr_stub` assembly and the `interrupt_handler` C function. At the end of the interrupt handler, `sched_tick()` returns either the current ESP (no switch) or a new ESP (switch). After the `popa` and `iret` pair, the CPU is running a different process — no separate switch routine needed.

### User Processes
Each user process runs in **ring 3** with its own page directory. `process_create_user()` allocates a page directory, maps code and stack pages, writes a ring-3 `iret` frame on the kernel stack, and hands the process to `sched_add()`. On process exit (`SYS_EXIT`), the kernel reclaims pages, kernel stack, and the PCB.

---

## 6. System Calls

User programs communicate with the kernel via `int 0x80`. The syscall ABI:
- `EAX` = syscall number
- `EBX–EDI` = arguments
- `EAX` (on return) = result

| # | Name | Description |
|---|------|-------------|
| 0 | `SYS_EXIT` | Terminate process with status code |
| 1 | `SYS_WRITE` | Write bytes to fd (fd 1 = console) |
| 2 | `SYS_READ` | Read bytes from fd (fd 0 = keyboard) |
| 3 | `SYS_GETPID` | Return the calling process's PID |
| 4 | `SYS_YIELD` | Voluntarily give up remaining timeslice |
| 10+ | GUI syscalls | Window create/draw/event/close operations |

GUI syscalls allow `.rox` processes to open windows, draw pixels, receive keyboard and mouse events, and close windows — all without needing shared memory or a display server.

---

## 7. Storage & Filesystem

### ATA PIO Driver
Communicates with the primary master IDE drive using polled Port I/O on `0x1F0–0x1F7`. Supports IDENTIFY, 28-bit LBA sector read and write. No DMA — straightforward and dependency-free.

### FAT32 Driver
Reads the BPB and EBPB from the boot sector, parses the FAT (File Allocation Table) for cluster chain traversal, supports both MBR-partitioned disks and super-floppy images. Implements: file open/read/write/seek/close, directory listing, `mkdir`, `unlink`, `stat`.

### VFS (Virtual Filesystem)
A thin **POSIX-like abstraction layer** above FAT32. Maintains a fixed-size file descriptor table (up to 16 open files). Higher-level code only ever calls VFS functions — the FAT32 and ATA drivers are never called directly. Provides: `vfs_open`, `vfs_read`, `vfs_write`, `vfs_seek`, `vfs_close`, `vfs_stat`, `vfs_readdir`, `vfs_mkdir`, `vfs_unlink`.

---

## 8. Device Drivers

| Driver | IRQ / Port | Description |
|--------|-----------|-------------|
| Keyboard | IRQ 1 | PS/2 scan-code set 1; 128-byte ring buffer; non-blocking + blocking read |
| Serial (COM1) | Polled | 9600 baud, 8N1, FIFO; all kernel log output |
| PIT (timer) | IRQ 0 | Channel 0 at ~100 Hz; drives scheduler tick + `pit_sleep_ms()` |
| ATA | Polled | Primary master, 28-bit LBA PIO |
| PS/2 Mouse | IRQ 12 | 3-button relative protocol; GUI mode cursor tracking |

---

## 9. Kernel Libraries

| Library | Description |
|---------|-------------|
| **stdio** | VGA 80×25 text console: `putchar`, `puts`, `puts_color`, `readline`, `scanf`, hardware cursor |
| **string** | `str*`, `mem*`, `atoi`, `itoa`, `sprintf` (`%c %d %x %s %%`) |
| **logging** | Leveled output (`debug/info/warning/error`) to framebuffer, serial, or both; `[LEVEL]` tags |
| **kheap** | `kmalloc` / `kfree` / `krealloc` with boundary-tag coalescing |
| **textbuf** | Line-oriented text buffer with cursor; used by the `rxt` editor via `tbuf_*` syscalls |

---

## 10. Shell (rosh)

An interactive command-line shell running as a scheduled kernel task. Key design points:

- **Line editor** — `readline()` with backspace and coloured prompt
- **Tokeniser** — splits input on whitespace into `argc/argv`
- **Command resolution** — `.rox` paths first, then `/bin/<cmd>.rox`, then built-in table
- **Boot-time filesystem** — creates `/bin`, `/etc`, `/home`, `/tmp`, `/var/log`, `/lib/ros` and writes stdlib files
- **Mode switching** — `nerd` (text) ↔ `gui` (graphical desktop)

Full command reference: [docs/kernel/shell.md](docs/kernel/shell.md)

---

## 11. rosc Compiler

A **native compiler** built into the kernel that turns `.ros` source files into `.rox` user-mode executables at runtime. The full pipeline runs inside the OS with no external tools needed.

```
.ros source  →  Lexer  →  Token stream
                           │
                        Parser  →  AST
                                    │
                                 Codegen  →  Flat x86-32 binary
                                               │
                                            .rox file (32-byte header + code + data)
```

**Implemented language features:**
- Types: `i32`, `u32`, `bool`, `str`
- Mutable/immutable variables (`let` / `let mut`)
- All arithmetic, bitwise, logical, and comparison operators
- Compound assignment: `+= -= *= /= %=`
- Control flow: `if / else if / else`, `while`, `for x in lo..hi`, `break`, `continue`
- Functions: `fn name(params) -> ret`, recursion, nested calls
- String literals with escape sequences
- Import system: `import gui`, `import math`, `import io`
- Built-ins: `print()`, `println()`, `getarg()`
- GUI syscalls for windowed applications

Full language reference: [docs/language/index.md](docs/language/index.md)

---

## 12. RandomOS Standard Library (`/lib/ros/`)

Three standard library files are compiled into `.ros` source and written to the VFS at every boot:

| Library | Purpose |
|---------|---------|
| `gui.ros` | All GUI widgets: colors, layout, buttons, checkboxes, sliders, radio buttons, progress bars, tooltips, notifications, stat cards |
| `math.ros` | Integer math utilities: `abs`, `min`, `max`, `clamp`, `isqrt`, `ipow`, `lerp`, `map_range` |
| `io.ros` | Text-buffer wrappers: open/close/save files, get lines, key input, cursor tracking |

Programs use them with `import <name>` at the top of a `.ros` file.

---

## 13. GUI Mode (VESA Framebuffer Desktop)

An optional graphical subsystem enabled with `-DGUI_MODE=ON`. Provides a complete pixel-level desktop environment:

| Layer | Component | Description |
|-------|-----------|-------------|
| Hardware | `fb.c` | VESA linear framebuffer from GRUB; double-buffered with a shadow back-buffer |
| Rendering | `gfx.c` | Rectangles, lines, circles, filled shapes, rounded rects |
| Text | `font.c` | Embedded 256-glyph 8×16 VGA bitmap font |
| Input | `mouse.c` | PS/2 3-button protocol, IRQ12, absolute cursor with sprite |
| Windowing | `wm.c` | Window list, Z-order, focus, title bar, drag, close events |
| Desktop | `desktop.c` | Wallpaper, taskbar, icon grid, `/etc/desktop.conf` persistence |
| Terminal | `gui_term.c` | `terminal_t` vtable so shell + `.rox` processes work unchanged |
| Init | `gui_init.c` | One-shot orchestrator called from `kmain` |

Both nerd and gui modes implement the same `terminal_t` interface — the shell, logger, and all built-in commands produce the same output regardless of mode.

---

## 14. Serial Monitor (Web UI)

A development tool in `serial-monitor/` (React + TypeScript + Vite) that streams COM1 output from the OS in real time:

- Colour-coded `[DEBUG]` / `[INFO]` / `[WARNING]` / `[ERROR]` levels
- Text and regex search, level filter toggles
- Pause/resume, auto-scroll, per-message timestamps
- Log download, keyboard shortcuts, auto-reconnect on disconnect

---

## 15. Documentation & Tooling

| Path | Contents |
|------|----------|
| `docs/language/` | Full `.ros` language reference, syntax, types, operators, stdlib |
| `docs/architecture/` | GDT, IDT, ISR, paging, PFA, TSS, multiboot |
| `docs/kernel/` | Shell, VFS, multitasking, process model, heap, logging, stdio, string |
| `docs/drivers/` | Keyboard, serial, PIT, ATA, FAT32, PIC |
| `docs/gui/` | GUI architecture, framebuffer, window manager, font, mouse |
| `compiler/compiler_features.md` | Language spec, type system, syscall ABI, phase plan |
| `compiler/docs_gen/docs_gen.py` | Docs generator: parses `.ros` files, extracts doc-comments, outputs Markdown |
| `cheatsheat/` | Quick-reference cards for shell, language, GUI, IDT, PIC, serial, interrupts |

---

## Summary Table

| Category | Status | Key Files |
|----------|--------|-----------|
| Boot / Multiboot | ✅ Complete | `asm/loader.s`, `c_files/src/kernel_init.c` |
| GDT / IDT / TSS | ✅ Complete | `asm/gdt.s`, `asm/idt.s`, `asm/isr.s`, `c_files/src/gdt.c` |
| PIC / PIT | ✅ Complete | `c_files/src/pic.c`, `c_files/src/pit.c` |
| Physical frame allocator | ✅ Complete | `c_files/src/pfa.c` |
| Kernel heap | ✅ Complete | `c_files/src/kheap.c` |
| Paging / VMM | ✅ Complete | `c_files/src/paging.c` |
| CFS Scheduler | ✅ Complete | `c_files/src/sched.c` |
| User processes (ring 3) | ✅ Complete | `c_files/src/process.c` |
| Syscall dispatcher | ✅ Complete | `c_files/src/syscall.c` |
| ATA PIO | ✅ Complete | `c_files/src/ata.c` |
| FAT32 | ✅ Complete | `c_files/src/fat32.c` |
| VFS | ✅ Complete | `c_files/src/vfs.c` |
| Keyboard driver | ✅ Complete | `c_files/src/keyboard.c` |
| Serial / COM1 | ✅ Complete | `c_files/src/serial.c` |
| PS/2 Mouse | ✅ Complete | `c_files/src/gui/mouse.c` |
| Text console (stdio) | ✅ Complete | `c_files/src/stdio.c` |
| Logging | ✅ Complete | `c_files/src/log.c` |
| Shell (rosh) | ✅ Complete | `c_files/src/shell.c` |
| rosc compiler | ✅ Complete | `compiler/src/` |
| `.ros` standard library | ✅ Complete | embedded in `shell.c`, written to `/lib/ros/` |
| GUI framebuffer | ✅ Complete | `c_files/src/gui/fb.c` |
| GUI window manager | ✅ Complete | `c_files/src/gui/wm.c` |
| GUI desktop | ✅ Complete | `c_files/src/gui/desktop.c` |
| rxt text editor | ✅ Complete | embedded `.ros` source in `shell.c` |
| Serial monitor (web) | ✅ Complete | `serial-monitor/` |
