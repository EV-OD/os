# RandomOS Language (.ros) — Documentation Index

The **RandomOS Language** (`.ros`) is a statically-typed, compiled language that runs
natively inside RandomOS. Source files are compiled by the `rosc` shell command into
`.rox` user-mode executables that run in ring-3 with their own page directory.

---

## Quick Start

```bash
sample hello          # generate hello.ros in the current directory
rosc hello.ros        # compile → hello.rox
./hello.rox           # run
```

---

## Documentation Sections

| Document | Description |
|----------|-------------|
| [syntax.md](syntax.md) | Complete grammar, literals, operators, precedence table |
| [types.md](types.md) | Type system: primitives, `str`, `bool`, mutability, casting |
| [control_flow.md](control_flow.md) | `if / else if / else`, `while`, `for`, `break`, `continue` |
| [functions.md](functions.md) | `fn` declarations, parameters, return types, recursion |
| [imports.md](imports.md) | `import` system, standard library modules |
| [builtins.md](builtins.md) | Built-in functions: `print`, `println`, `getarg`, GUI syscalls |
| [stdlib.md](stdlib.md) | Full standard library reference (`gui`, `math`, `io`) |
| [examples.md](examples.md) | Annotated example programs |

---

## Language at a Glance

```ros
// Variables
let x: i32 = 10
let mut count: i32 = 0

// Functions
fn add(a: i32, b: i32) -> i32 {
    return a + b
}

// Control flow
if count > 0 {
    println("positive")
} else {
    println("zero or negative")
}

// Loop
let mut i: i32 = 0
while i < 5 {
    print(i)
    i += 1
}

// For range
for n in 1..11 {
    print(n)
}

// Imports
import math
let big: i32 = max(42, 99)

import gui
let win: i32 = gui_window(50, 50, 640, 480, "My Window")
```

---

## Execution Model

| Property | Value |
|----------|-------|
| Architecture | x86 (IA-32), 32-bit protected mode |
| Privilege | Ring 3 (user mode) |
| Output format | `.rox` (32-byte header + flat x86 code + data) |
| Entry point | `main()` if defined, otherwise top-level statements |
| System calls | `int 0x80` — `EAX` = number, `EBX–EDI` = args |
| Code base address | `0x08048000` |
| Stack base | `0xBFFFF000` |

---

## Generating Docs

Use the docs generator to produce Markdown API references from `.ros` source files:

```bash
python3 compiler/docs_gen/docs_gen.py /lib/ros/gui.ros > docs/language/gen/gui_api.md
python3 compiler/docs_gen/docs_gen.py --all-stdlib    > docs/language/gen/stdlib_full.md
```

See [compiler/docs_gen/docs_gen.py](../../compiler/docs_gen/docs_gen.py) for full usage.
