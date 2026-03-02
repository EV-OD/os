# RandomOS Language — Compiler Features

> Design document for the native compiler that ships with RandomOS.
> The compiler runs **inside the OS** (as a shell built-in) and produces **.rox executables** containing flat **32-bit x86 machine code** that execute in **ring 3 (user mode)**.

---

## 1. Compilation Pipeline

```
Source (.ros)
    │
    ▼
┌──────────┐
│  Lexer   │  → Token stream
└──────────┘
    │
    ▼
┌──────────┐
│  Parser  │  → Abstract Syntax Tree (AST)
└──────────┘
    │
    ▼
┌──────────┐
│ Code Gen │  → Flat x86-32 machine code + embedded string data
└──────────┘
    │
    ▼
┌──────────┐
│.rox Write│  → ROX header (32 B) + code + data section
└──────────┘
    │
    ▼
  program.rox   (loadable & runnable by rosh)
```

Each stage is a **separate module** (source file) so it can be tested and evolved independently.

### Phase 1 (Current Implementation)

The Phase 1 compiler supports integer arithmetic with `let` bindings.  All expressions
are **constant-folded at compile time**.  The generated binary uses `int 0x80` syscalls
to print each binding's result and then exits cleanly.

**Shell workflow:**
```
$ sample hello.ros          # create a sample .ros source file
$ cat hello.ros             # view the source
$ rosc hello.ros            # compile → hello.rox
$ ./hello.rox               # run in user mode (ring 3)
```

---

## 2. Target & Execution Model

| Property | Value |
|---|---|
| Architecture | x86 (IA-32), 32-bit protected mode |
| Privilege level | Ring 3 (user mode) |
| Output format | .rox executable (32-byte header + flat code + data) |
| System calls | Via `int 0x80` (ABI: EAX=nr, EBX-EDI=args, return in EAX) |
| Memory model | Flat, single segment, code at 0x08048000, stack at 0xBFFFF000 |
| Calling convention | cdecl (caller cleans stack, EAX for return value) |
| Process lifecycle | `rox_load_and_run()` → `process_create_user()` → `sched_add()` → `process_wait()` → `process_destroy()` |

### .rox Executable Format

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 | 4 | magic | `0x524F5821` ("ROX!" ASCII) |
| 0x04 | 4 | version | Format version (currently 1) |
| 0x08 | 4 | entry_offset | Entry point offset from start of code section |
| 0x0C | 4 | code_size | Size of code+data section in bytes |
| 0x10 | 4 | flags | Reserved (0) |
| 0x14 | 12 | name | Null-terminated program name |
| 0x20 | ... | code+data | Flat binary: instructions followed by string data |

---

## 3. Type System

### 3.1 Primitive Types (Phase 1: i32, u32, bool)

| Type | Size | Description |
|---|---|---|
| `i32` | 4 bytes | Signed 32-bit integer (default integer type) |
| `u32` | 4 bytes | Unsigned 32-bit integer |
| `bool` | 1 byte | `true` / `false` |

### 3.2 Future Types (Phase 2+)

| Type | Size | Description |
|---|---|---|
| `i8` | 1 byte | Signed 8-bit integer (also used for characters) |
| `i16` | 2 bytes | Signed 16-bit integer |
| `u8` | 1 byte | Unsigned 8-bit integer / byte |
| `u16` | 2 bytes | Unsigned 16-bit integer |
| `void` | 0 | No value (for functions only) |
| Arrays | e.g. `i32[10]` | Fixed-size, stack or global |
| Pointers | e.g. `*u8` | Raw pointer |
| Structs | e.g. `struct Packet {}` | Value types |
| Strings | `str` | Pointer + length pair |

### 3.3 Type Rules

- **Statically typed** — every variable has a type known at compile time.
- Phase 1: explicit type annotation required (`let x: i32 = ...`).
- Future: type inference on `let` bindings (`let x = 42` infers `i32`).
- **Explicit casts** required between different-width integers (`x as u8`).

---

## 4. Syscall Interface

The compiler generates direct `int 0x80` instructions.  The ABI matches the kernel's
syscall dispatcher:

| Register | Purpose |
|----------|---------|
| EAX | Syscall number |
| EBX | Argument 1 |
| ECX | Argument 2 |
| EDX | Argument 3 |
| ESI | Argument 4 |
| EDI | Argument 5 |
| EAX (return) | Return value |

### Current Syscalls

| Number | Name | Signature |
|--------|------|-----------|
| 0 | `SYS_EXIT` | `void _exit(int status)` |
| 1 | `SYS_WRITE` | `int write(int fd, const char *buf, int len)` |
| 2 | `SYS_READ` | `int read(int fd, char *buf, int len)` |
| 3 | `SYS_GETPID` | `int getpid(void)` |
| 4 | `SYS_YIELD` | `void yield(void)` |
| 5 | `SYS_SBRK` | `void *sbrk(int increment)` (future) |

### Phase 1 Code Generation

For each `let` binding, the codegen:
1. Constant-folds the expression at compile time
2. Builds an embedded string `"name = value\n"`
3. Emits: `mov eax, 1; mov ebx, 1; mov ecx, <str_addr>; mov edx, <len>; int 0x80`

For each `print()` call:
- `print("string literal")` — embeds the string in the data section, emits SYS_WRITE
- `print(expr)` — evaluates `expr` at compile time, converts to `"value\n"`, emits SYS_WRITE
- Supports escape sequences in string literals: `\n`, `\t`, `\\`, `\"`

After all statements, emits: `mov eax, 0; mov ebx, 0; int 0x80; hlt`

String addresses are absolute (base = `0x08048000 + code_length`).

---

## 5. Hardware Access (Planned)

### 5.1 Port I/O (via syscalls)

```
port_out(port: u16, value: u8)      // outb
let b: u8  = port_in(port: u16)    // inb
```

### 5.2 Inline Assembly

```
asm {
    mov eax, 1
    int 0x80
}
```

### 5.3 Memory-Mapped I/O

```
let fb = 0x000B8000 as *u8
@(fb + 0) = 0x41
```

---

## 6. Error Reporting

- Every error message includes **category, message, line, column**.
- Errors are categorised: `[lexer]`, `[parser]`, `[codegen]`.
- The error subsystem tracks a global error count per compilation unit.

Example:
```
error[parser]: expected ':' after variable name  (line 3, col 8)
```

---

## 7. Compiler CLI (Shell Built-in)

```
rosc <input.ros> [output.rox]

  Reads <input.ros> from the VFS, compiles it, and writes a .rox
  executable.  If output.rox is omitted, replaces .ros with .rox.

sample [filename.ros]

  Creates a sample .ros source file in the current directory.
  Default filename: hello.ros
```

---

## 8. Shell Integration

### Command Resolution Order (rosh)

1. Absolute path (`/bin/prog.rox`) → load directly
2. Relative path (`./prog.rox`) → resolve from cwd
3. Bare `.rox` name (`prog.rox`) → resolve from cwd
4. Bare command (`prog`) → try `/bin/prog.rox`
5. Built-in command table
6. "command not found"

### Execution Flow

```
shell input: "./hello.rox"
    │
    ▼
resolve_and_execute()
    │ resolve path → "/home/hello.rox"
    ▼
rox_load_and_run("/home/hello.rox")
    │ read .rox header, validate, read code
    ▼
process_create_user("hello", code, size, 0, nice=0)
    │ create page directory
    │ map code at 0x08048000 (4KB pages)
    │ map stack at 0xBFFFF000
    │ build ring-3 iret frame on kernel stack
    ▼
sched_add(proc)       ← process enters CFS run queue
    │
    ▼
process_wait(pid)     ← shell blocks (sti; hlt) until child exits
    │
    ▼
process_destroy(proc) ← free page dir, kernel stack, frames
```

---

## 9. Modular Source Layout

```
compiler/
├── compiler_features.md      ← this file
├── include/
│   └── common.h              ← shared typedefs, macros, limits
├── src/
│   ├── rosc.c                ← compiler driver (rosc_compile + legacy demo)
│   ├── lexer.c / lexer.h     ← tokeniser
│   ├── parser.c / parser.h   ← recursive-descent parser → AST
│   ├── ast.c / ast.h         ← AST node definitions & helpers
│   ├── codegen.c / codegen.h ← x86-32 machine code emitter (user-mode)
│   └── error.c / error.h     ← diagnostic formatting
└── tests/
    └── programs/             ← sample .ros programs

c_files/
├── includes/
│   ├── rosc.h                ← compiler public API (rosc_compile)
│   ├── rox.h                 ← .rox format & loader API
│   ├── shell.h               ← shell with rosc + sample commands
│   ├── process.h             ← process_create_user() for ring-3
│   └── syscall.h             ← int 0x80 dispatcher
├── src/
│   ├── rox.c                 ← .rox loader → process_create_user → wait
│   ├── shell.c               ← cmd_rosc, cmd_sample, ./path resolution
│   ├── process.c             ← user-mode process creation
│   ├── syscall.c             ← syscall handlers (exit, write, read, ...)
│   └── sched.c               ← CFS scheduler with CR3 switching
```

---

## 10. Phase Plan

| Phase | Milestone | Status | Key Deliverables |
|---|---|---|---|
| **Phase 1** | Minimal viable compiler | **Done** ✅ | Lexer, parser, codegen for integer arithmetic, `let`/`let mut`, bitwise/logical operators, constant folding. `.rox` format (32-byte header + flat x86-32). User-mode execution via `int 0x80`. Shell commands `rosc` and `sample`. |
| **Phase 2** | Control flow & functions | **Done** ✅ | `if`/`else-if`/`else`, `while`, `for x in lo..hi`, `break`, `continue`, user `fn` with return values, recursion, forward-declaration pass, compound assignment (`+=` `-=` `*=` `/=` `%=`), `print`/`println` built-ins. |
| **Phase 3** | Type system & stdlib | **Done** ✅ | `i32`, `u32`, `bool`, `str` (pointer to literal), `as` cast, `getarg` built-in, `import` system. Stdlib modules `math.ros` (8 fns), `io.ros` (11 fns), `gui.ros` (full widget toolkit + color constants + layout helpers). GUI syscalls `10+` for window management, drawing, events, mouse. |
| **Phase 4** | Hardware access | **Partial** 🔄 | GUI syscalls expose framebuffer drawing and event I/O from user mode. VFS file I/O via `io.ros`. Full inline asm, `port_in`/`port_out` syscalls, and raw MMIO helpers remain planned. |
| **Phase 5** | Optimiser | **Partial** 🔄 | Constant folding implemented. Dead-code elimination, common-sub-expression elimination, and a proper register allocator remain planned. |
| **Phase 6** | Tooling & debugger | **Partial** 🔄 | `docs_gen.py` generates Markdown API docs from `///` doc-comments. `extract_stdlib.py` extracts embedded `.ros` sources from `shell.c`. REPL, step debugger, and disassembler remain planned. |
