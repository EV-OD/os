# .ros Language — Import System

---

## Overview

The `import` statement loads a standard library `.ros` file from `/lib/ros/` and
makes all of its functions available in the current file without a namespace prefix.

```ros
import gui     // loads /lib/ros/gui.ros
import math    // loads /lib/ros/math.ros
import io      // loads /lib/ros/io.ros
```

Import declarations must appear **at the top of the file**, before any function
definitions or statements.

---

## How It Works

When `rosc` encounters `import <name>`:
1. It opens `/lib/ros/<name>.ros` from the VFS.
2. It lexes and parses the library source into the same AST as the main file.
3. All functions from the library become available for calls in the main file.
4. The library's machine code is emitted into the same `.rox` code section.

There is no separate linking step — everything compiles into one flat binary.

---

## Available Standard Libraries

| Module | File | Description |
|--------|------|-------------|
| `gui` | `/lib/ros/gui.ros` | GUI widgets, layout helpers, color constants |
| `math` | `/lib/ros/math.ros` | Integer math: abs, min, max, clamp, isqrt, ipow, lerp, map_range |
| `io` | `/lib/ros/io.ros` | Text-buffer file I/O, cursor tracking, line access |

See [stdlib.md](stdlib.md) for the complete API of each module.

---

## Example — Using math

```ros
import math

fn main() {
    let a: i32 = abs(-42)          // 42
    let b: i32 = clamp(150, 0, 100) // 100
    let c: i32 = isqrt(144)         // 12
    println(a)
    println(b)
    println(c)
}
```

---

## Example — Using gui

```ros
import gui

fn main() {
    let win: i32 = gui_window(50, 50, 400, 300, "Hello Window")
    let mut running: i32 = 1

    while running == 1 {
        let ms: i32  = gui_mouse(win)
        let key: i32 = gui_poll(win)

        gui_fill(win, COL_BG())
        gui_text(win, 20, 20, "Hello from .ros!", COL_TEXT())
        gui_flush(win)

        if key == EV_CLOSE() { running = 0 }
        if key == 113        { running = 0 }   // 'q'
    }
    gui_close(win)
}
```

---

## Example — Using io (rxt-style editor)

```ros
import io

fn main() {
    let tb: i32 = io_open(getarg(1))   // open file named in argv[1]
    let lines: i32 = io_lines(tb)

    let mut i: i32 = 0
    while i < lines {
        println(io_getline(tb, i))
        i += 1
    }

    io_close(tb)
}
```

---

## Writing Your Own Libraries (planned)

In a future phase, any `.ros` file can be imported using a relative path:

```ros
import "./mylib"     // planned — not yet supported
```

For now only the three standard libraries in `/lib/ros/` are importable.
