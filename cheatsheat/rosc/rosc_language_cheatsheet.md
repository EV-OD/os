# .ros Language — Quick Reference

## File Structure

```ros
// 1. imports (must be first)
import gui
import math

// 2. functions (any order; forward refs OK)
fn helper(x: i32) -> i32 { return x * 2 }

// 3. main() or top-level statements
fn main() {
    let val: i32 = helper(21)
    println(val)
}
```

---

## Types & Variables

```ros
let x: i32 = 42           // immutable i32
let mut n: i32 = 0         // mutable i32
let flag: bool = true
let msg: str = "hello\n"
```

| Type | Size | Range |
|------|------|-------|
| `i32` | 4 B | −2 147 483 648 … 2 147 483 647 |
| `u32` | 4 B | 0 … 4 294 967 295 |
| `bool` | 1 B | `true` / `false` |
| `str` | 4 B | pointer to null-terminated literal |

---

## Operators

```
Arithmetic:  +  -  *  /  %
Bitwise:     &  |  ^  ~  <<  >>
Comparison:  ==  !=  <  >  <=  >=
Logical:     &&  ||  !
Compound:    +=  -=  *=  /=  %=
```

**Precedence (high → low):** unary → `* / %` → `+ -` → `<< >>` → `& ^ |` → `== != < > <= >=` → `&& ||`

---

## Control Flow

```ros
// if
if x > 0 { println("pos") } else if x == 0 { println("zero") } else { println("neg") }

// while
let mut i: i32 = 0
while i < 10 { print(i)   i += 1 }

// for range  (exclusive upper bound)
for k in 1..6 { println(k) }   // 1 2 3 4 5

// break / continue
while true {
    if done == 1 { break }
    if skip == 1 { continue }
}
```

---

## Functions

```ros
fn name(a: i32, b: i32) -> i32 {
    return a + b
}
fn void_fn(s: str) {
    println(s)
    return          // optional bare return
}
```

---

## Built-ins (no import needed)

```ros
print("text")          // print without newline
println(42)            // print + newline
getarg(1)              // argv[1] as str handle (0 if missing)
```

---

## Imports & Stdlib

```ros
import math    // abs min max clamp isqrt ipow lerp map_range
import io      // io_open io_close io_save io_getline io_lines io_cursor io_key
import gui     // gui_window gui_fill gui_text gui_flush gui_poll gui_mouse ...
```

---

## math — Selected Functions

```ros
abs(-5)            // 5
min(3, 7)          // 3
max(3, 7)          // 7
clamp(200, 0, 100) // 100
isqrt(144)         // 12
ipow(2, 8)         // 256
lerp(0, 100, 3, 10) // 30
map_range(50, 0, 100, 0, 255) // 127
```

---

## io — Selected Functions

```ros
let tb: i32 = io_open("/home/file.txt")
let n: i32  = io_lines(tb)
println(io_getline(tb, 0))  // first line
io_save(tb)
io_close(tb)
```

---

## GUI Frame Loop Skeleton

```ros
import gui

fn main() {
    let win: i32 = gui_window(50, 50, 400, 300, "Title")
    let mut running: i32 = 1

    while running == 1 {
        let ms:  i32 = gui_mouse(win)
        let key: i32 = gui_poll(win)

        gui_fill(win, COL_BG())
        // ... draw ...
        gui_flush(win)

        if key == EV_CLOSE() { running = 0 }
        if key == 113        { running = 0 }   // q
    }
    gui_close(win)
}
```

---

## Compile & Run

```
rosc hello.ros              # → hello.rox
rosc -f hello.ros out.rox   # force overwrite
./hello.rox                 # run
```

---

## Sample Programs (generate in shell)

```
sample list      # show all templates
sample hello     # hello.ros  — basic print
sample loops     # loops.ros  — while / for / break / continue
sample funcs     # funcs.ros  — functions, recursion
sample gui       # gui.ros    — GUI widgets
sample mu        # mu_demo.ros — full widget demo
sample test      # test_all.ros — PASS/FAIL test suite
```

---

## Escape Sequences

| Sequence | Char |
|----------|------|
| `\n` | newline |
| `\t` | tab |
| `\\` | backslash |
| `\"` | double quote |
