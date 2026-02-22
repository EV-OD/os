# RandomOS Language — Compiler Features

> Design document for the native compiler that ships with RandomOS.
> The compiler runs **on the host** (Linux x86) and produces flat **32-bit x86 machine code** binaries that execute directly inside RandomOS (ring 3 / user mode).

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
┌───────────────┐
│ Semantic Pass │  → Type-checked / annotated AST
└───────────────┘
    │
    ▼
┌──────────┐
│ IR Gen   │  → Flat intermediate representation (three-address code)
└──────────┘
    │
    ▼
┌──────────┐
│ Optimizer│  → Optimised IR (constant folding, dead-code elimination)
└──────────┘
    │
    ▼
┌──────────┐
│ Code Gen │  → Raw x86-32 machine code (flat binary)
└──────────┘
    │
    ▼
  program.bin   (loadable by RandomOS)
```

Each stage is a **separate module** (source file) so it can be tested and evolved independently.

---

## 2. Target & Execution Model

| Property | Value |
|---|---|
| Architecture | x86 (IA-32), 32-bit protected mode |
| Privilege level | Ring 3 (user mode) |
| Output format | Flat binary (no ELF headers for now; later: simple RandomOS executable format) |
| System calls | Via `int 0x80` (or a dedicated software interrupt chosen by RandomOS) |
| Memory model | Flat, single segment, all pointers 32-bit |
| Calling convention | cdecl (caller cleans stack, EAX for return value) |

---

## 3. Type System

### 3.1 Primitive Types

| Type | Size | Description |
|---|---|---|
| `i8` | 1 byte | Signed 8-bit integer (also used for characters) |
| `i16` | 2 bytes | Signed 16-bit integer |
| `i32` | 4 bytes | Signed 32-bit integer (default integer type) |
| `u8` | 1 byte | Unsigned 8-bit integer / byte |
| `u16` | 2 bytes | Unsigned 16-bit integer |
| `u32` | 4 bytes | Unsigned 32-bit integer |
| `bool` | 1 byte | `true` / `false` |
| `void` | 0 | No value (for functions only) |

### 3.2 Compound Types

| Type | Example | Notes |
|---|---|---|
| Arrays | `i32[10]` | Fixed-size, stack or global |
| Pointers | `*u8` | Raw pointer — dereferenced with `@` operator |
| Structs | `struct Packet { ... }` | Value types, passed by copy unless pointer |
| Strings | `str` | Pointer + length pair, **not** null-terminated internally |

### 3.3 Type Rules

- **Statically typed** — every variable has a type known at compile time.
- **Type inference** on `let` bindings: `let x = 42` infers `i32`.
- **Explicit casts** required between different-width integers (`x as u8`).
- No implicit pointer ↔ integer conversion (must use `as`).

---

## 4. Hardware Access (User-Level)

One of the language's key goals is giving the programmer **controlled access to x86 hardware** without kernel privileges.

### 4.1 Port I/O (via syscalls)

```
// Exposed as built-in functions that compile to syscall wrappers.
// The kernel validates the port range before executing.

port_out(port: u16, value: u8)      // outb
port_out16(port: u16, value: u16)   // outw
let b: u8  = port_in(port: u16)    // inb
let w: u16 = port_in16(port: u16)  // inw
```

> The kernel will maintain an I/O permission bitmap (IOPB) so only approved ports are reachable from ring 3.
> For Phase 1, these simply emit `int 0x80` with the appropriate syscall number.

### 4.2 Inline Assembly

For anything the type system cannot express:

```
asm {
    mov eax, 1
    int 0x80
}
```

- Inline `asm` blocks are **pass-through**: the compiler copies the assembly text into the code-gen stage verbatim and assembles it.
- An extended form lets you bind variables:

```
let result: u32
asm (result <- eax) {
    mov eax, 0x0F
    cpuid
}
```

### 4.3 Memory-Mapped I/O

```
let fb = 0x000B8000 as *u8      // VGA framebuffer address
@(fb + 0) = 0x41                // write 'A' to top-left cell
@(fb + 1) = 0x0F                // white-on-black attribute
```

Direct pointer arithmetic + dereference gives full MMIO control.

---

## 5. Syscall Interface

The compiler provides **built-in wrappers** for every RandomOS system call. Internally each wrapper:

1. Places the syscall number in `eax`.
2. Places arguments in `ebx`, `ecx`, `edx`, `esi`, `edi` (in order).
3. Executes `int 0x80`.
4. Reads the return value from `eax`.

Initial syscall set (mirrors current kernel capabilities):

| Number | Name | Description |
|---|---|---|
| 0 | `sys_exit` | Terminate process with exit code |
| 1 | `sys_write` | Write buffer to framebuffer / serial |
| 2 | `sys_read` | Blocking read from keyboard |
| 3 | `sys_port_in` | Read from I/O port (validated) |
| 4 | `sys_port_out` | Write to I/O port (validated) |
| 5 | `sys_sleep` | Yield for N timer ticks |

---

## 6. Optimisations (Planned)

The compiler will ship with a small set of safe, impactful optimisations:

| Pass | Stage | Description |
|---|---|---|
| Constant folding | IR | Evaluate constant expressions at compile time |
| Dead code elimination | IR | Remove unreachable/unused code |
| Strength reduction | IR | Replace expensive ops (mul by power-of-2 → shift) |
| Register allocation | Code Gen | Linear-scan allocator over x86 GP registers |
| Peephole | Code Gen | Combine redundant `mov`/`push`/`pop` sequences |

All optimisations are optional and can be toggled with `-O0` (none) through `-O2`.

---

## 7. Error Reporting

- Every error message includes **file, line, column** and a source snippet with a caret (`^`) pointing to the offending token.
- Warnings for: unused variables, implicit truncation, unreachable code.
- Errors are categorised: `[lexer]`, `[parser]`, `[type]`, `[codegen]`.

Example:
```
error[type]: cannot assign `u32` to `u8` without explicit cast
 --> main.ros:14:9
   |
14 |     let b: u8 = big_value
   |         ^ expected `u8`, found `u32`
   |
   = help: use `big_value as u8` to truncate
```

---

## 8. Compiler CLI

```
rosc <input.ros> [options]

Options:
  -o <file>       Output binary path (default: a.bin)
  -O<level>       Optimisation level: 0, 1, 2 (default: 0)
  --emit-ir       Dump IR to stdout (for debugging)
  --emit-asm      Dump generated assembly to stdout
  --emit-tokens   Dump lexer token stream
  --emit-ast      Dump parsed AST as indented text
  -v, --verbose   Verbose compilation log
  -h, --help      Show help
```

---

## 9. Modular Source Layout

```
compiler/
├── compiler_features.md      ← this file
├── language_structure.md     ← language syntax & semantics reference
├── src/
│   ├── main.c                ← CLI entry point, argument parsing
│   ├── lexer.c / lexer.h     ← Tokeniser
│   ├── parser.c / parser.h   ← Recursive-descent parser → AST
│   ├── ast.c / ast.h         ← AST node definitions & helpers
│   ├── sema.c / sema.h       ← Semantic analysis & type checking
│   ├── ir.c / ir.h           ← IR generation (three-address code)
│   ├── opt.c / opt.h         ← IR-level optimisations
│   ├── codegen.c / codegen.h ← x86-32 machine code emitter
│   ├── elf.c / elf.h         ← (future) simple executable format writer
│   └── error.c / error.h     ← Diagnostic formatting
├── include/
│   └── common.h              ← Shared typedefs, macros, limits
├── stdlib/
│   └── core.ros              ← Minimal standard library (print, read, etc.)
├── tests/
│   ├── test_lexer.c
│   ├── test_parser.c
│   └── programs/             ← Sample .ros programs for end-to-end testing
│       ├── hello.ros
│       └── fibonacci.ros
└── CMakeLists.txt            ← Build integration
```

---

## 10. Phase Plan

| Phase | Milestone | Key Deliverables |
|---|---|---|
| **Phase 1** | Minimal viable compiler | Lexer, parser, code-gen for integer arithmetic + `print`. Flat binary output. |
| **Phase 2** | Control flow & functions | `if`/`else`, `while`, `for`, user functions, call stack. |
| **Phase 3** | Type system | Structs, arrays, pointers, type checking, `str` type. |
| **Phase 4** | Hardware access | Inline asm, `port_in`/`port_out` syscalls, MMIO helpers. |
| **Phase 5** | Optimiser | Constant folding, dead-code elimination, register allocator. |
| **Phase 6** | Standard library & tooling | `core.ros` stdlib, REPL/shell integration, debugger hooks. |
