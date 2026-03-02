# Shell (rosh) — Rabin OS Shell

## Overview

RandomOS includes a built-in kernel shell — **rosh** (Rabin OS Shell) — featuring a
text-based boot animation, POSIX-like filesystem commands, the `rosc` native compiler,
a `sample` program generator, an embedded `rxt` text editor, a desktop icon manager,
and full support for external `.rox` user-mode executables.

Two OS modes are available:
- **nerd mode** — classic 80×25 VGA text terminal (default)
- **gui mode** — VESA framebuffer graphical desktop (compile-time: `-DGUI_MODE=ON`)

---

## Boot Flow

```
kernel_init()
  │  Hardware: GDT, IDT, PFA, kheap, PIT, keyboard, serial, ATA, FAT32, TSS
  │
  ├── Smoke tests       heap, PFA, VFS  (serial only)
  │
  ├── shell_init()
  │     Creates:   /bin  /etc  /home  /tmp  /var/log  /lib/ros
  │     Writes:    /etc/hostname  /etc/motd
  │     stdlib:    /lib/ros/gui.ros  math.ros  io.ros
  │
  ├── boot_animation()  ASCII art + progress bar
  │
  └── shell_run()       readline → tokenise → resolve → dispatch  (never returns)
        [GUI mode]  →   desktop_run()  (window manager event loop)
```

---

## OS Modes

| Mode | Build flag | Description |
|------|------------|-------------|
| `nerd` (default) | _(none)_ | 80×25 VGA text terminal |
| `gui` | `-DGUI_MODE=ON` | VESA pixel desktop, mouse, windows |

```
mode              # show current mode
mode nerd         # switch to text shell
mode gui          # switch to GUI desktop
```

---

## Command Resolution Order

1. Starts with `./` or `/` → run literal `.rox` path
2. Ends with `.rox` → resolve relative to cwd
3. `/bin/<cmd>.rox` exists → load via `rox_load_and_run()`
4. Built-in table match → invoke handler
5. `rosh: <cmd>: command not found`

---

## Built-in Commands

### Filesystem

| Command | Usage | Description |
|---------|-------|-------------|
| `ls` | `ls [-a] [path]` | List directory; `-a` shows dot-files |
| `cd` | `cd [path]` | Change directory; bare `cd` → `/home` |
| `cat` | `cat <file>` | Print file contents |
| `mkdir` | `mkdir <path>` | Create directory |
| `touch` | `touch <file>` | Create empty file |
| `rm` | `rm <file>` | Remove file |
| `write` | `write <file> <text...>` | Overwrite file with text |
| `pwd` | `pwd` | Print working directory |
| `stat` | `stat <path>` | Show size, type, cluster |

### System

| Command | Usage | Description |
|---------|-------|-------------|
| `help` | `help` | List all commands |
| `clear` | `clear` | Clear screen |
| `echo` | `echo <text...>` | Print to stdout |
| `uname` | `uname` | OS name, version, arch |
| `whoami` | `whoami` | Current user |
| `mode` | `mode [nerd\|gui]` | Show/set OS mode |

### Compiler & Tools

| Command | Usage | Description |
|---------|-------|-------------|
| `rosc` | `rosc [-f] <src.ros> [out.rox]` | Compile `.ros` → `.rox` |
| `sample` | `sample [name\|list]` | Generate a sample `.ros` program |
| `setup` | `setup` | Install `rxt` editor and `term` to `/bin/` |

### GUI / Desktop

| Command | Usage | Description |
|---------|-------|-------------|
| `microui` | `microui` | Launch microui demo (GUI mode only) |
| `desktop add` | `desktop add <name\|path.rox>` | Add desktop icon |
| `desktop delete` | `desktop delete <label>` | Remove desktop icon |

---

## rosc — Compiler Command

```
rosc <src.ros>                  # compile → src.rox
rosc <src.ros> <out.rox>        # compile → custom output path
rosc -f <src.ros> [out.rox]     # -f: overwrite existing output
```

Typical workflow:
```
sample hello          # generate hello.ros in cwd
cat hello.ros
rosc hello.ros        # → hello.rox
./hello.rox           # run in ring-3 user mode
```

---

## sample — Program Templates

```
sample list           # show all available templates
sample hello          # hello.ros  — variables, print
sample strings        # strings.ros — string literals, escapes
sample math           # math.ros  — arithmetic
sample fib            # fib.ros   — fibonacci (let chain)
sample loops          # loops.ros — while, if/else, for, break, continue
sample funcs          # funcs.ros — functions, recursion
sample gui            # gui.ros   — GUI library: widgets, layout
sample mu             # mu_demo.ros — full microui widget demo
sample test           # test_all.ros — language test suite (PASS/FAIL)
```

---

## setup — Install rxt & term

Compiles and installs two built-in tools to `/bin/`:

| Tool | Binary | Description |
|------|--------|-------------|
| `rxt` | `/bin/rxt.rox` | Text editor — GUI window, line numbers, scrolling cursor, Ctrl+S / Ctrl+Q |
| `term` | `/bin/term.rox` | Terminal spawner — opens a new GUI terminal via `spawn_term()` |

---

## Standard Library (`/lib/ros/`)

Written to the VFS at boot by `shell_init_stdlib()`:

| File | `import` name | Contents |
|------|---------------|----------|
| `/lib/ros/gui.ros` | `import gui` | Colors, layout helpers, buttons, checkbox, radio, slider, toggle, progress, textbox, scrollbar, tooltip, notify, tag, stat card, badge, divider, icon |
| `/lib/ros/math.ros` | `import math` | `abs`, `min`, `max`, `clamp`, `isqrt`, `ipow`, `lerp`, `map_range` |
| `/lib/ros/io.ros` | `import io` | `io_open/close/save`, `io_getline`, `io_key`, `io_lines`, `io_cursor`, `io_itoa`, `print_sep`, `print_header` |

See [docs/language/stdlib.md](../language/stdlib.md) for the full stdlib reference.

---

## .rox Executable Format

32-byte header followed by a flat code+data section:

| Offset | Size | Field | Value |
|--------|------|-------|-------|
| `0x00` | 4 | `magic` | `0x524F5821` (`"ROX!"`) |
| `0x04` | 4 | `version` | `1` |
| `0x08` | 4 | `entry_offset` | Byte offset of entry within code |
| `0x0C` | 4 | `code_size` | Total bytes of code+data |
| `0x10` | 4 | `flags` | Reserved (`0`) |
| `0x14` | 12 | `name` | Null-terminated name |
| `0x20` | … | code+data | Flat x86-32 machine code |

Execution: code mapped at `0x08048000`, stack at `0xBFFFF000`, ring-3 iret frame built by `process_create_user()`, kernel entry via `int 0x80`.

---

## Directory Structure (created at boot)

```
/
├── bin/              .rox executables  (rxt.rox, term.rox, user programs)
├── etc/
│   ├── hostname      "RandomOS"
│   ├── motd          Message of the day
│   └── desktop.conf  Desktop icon list (GUI mode)
├── home/             User home directories
├── tmp/              Compiler staging, temporary files
├── var/log/          Log files
└── lib/ros/
    ├── gui.ros       GUI widget library
    ├── math.ros      Math utilities
    └── io.ros        Text-buffer I/O library
```

---

## path Helper

All path arguments pass through `resolve_path()`:
- Absolute path → used as-is
- Relative path → prefixed with cwd
- Trailing `/` removed unless root

---

## Source Reference

| Symbol | File | Approx. line |
|--------|------|-------|
| `shell_init()` | `c_files/src/shell.c` | ~843 |
| `shell_run()` | `c_files/src/shell.c` | ~896 |
| `builtins[]` table | `c_files/src/shell.c` | ~87 |
| `shell_init_stdlib()` | `c_files/src/shell.c` | ~706 |
| `RXT_ROS` / `TERM_ROS` | `c_files/src/shell.c` | ~725 |
| `sample_table[]` | `c_files/src/shell.c` | ~1880 |
