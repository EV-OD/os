# .ros Language — Example Programs

All examples can be generated with the `sample` shell command and then compiled with `rosc`.

---

## Hello World

```ros
// hello.ros
// Compile: rosc hello.ros
// Run:     ./hello.rox

print("Hello, RandomOS!\n")

let x: i32 = 42
let y: i32 = x * 2 + 8
let answer: i32 = (x + y) / 2

print("The answer is: ")
print(answer)
print("\n")
```

---

## Variables & Arithmetic

```ros
// math.ros
print("=== Math Demo ===\n")

let a: i32 = 100
let b: i32 = 37

println(a + b)          // 137
println(a - b)          // 63
println(a * b)          // 3700
println(a / b)          // 2
println(a % b)          // 26

let nested: i32 = (a + b) * (a - b) / 10
println(nested)         // 85
```

---

## Loops & Conditionals

```ros
// loops.ros
fn main() {
    // Count up with while
    let mut i: i32 = 1
    while i <= 5 {
        println(i)
        i += 1
    }

    // for range
    for k in 0..10 {
        if k % 2 == 0 {
            print("even: ")
        } else {
            print("odd:  ")
        }
        println(k)
    }

    // break and continue
    let mut n: i32 = 0
    while true {
        n += 1
        if n % 3 == 0 { continue }
        if n > 9      { break    }
        println(n)
    }
}
```

---

## Functions & Recursion

```ros
// funcs.ros
fn add(a: i32, b: i32) -> i32 {
    return a + b
}

fn factorial(n: i32) -> i32 {
    if n <= 1 { return 1 }
    return n * factorial(n - 1)
}

fn fib(n: i32) -> i32 {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}

fn main() {
    println(add(12, 30))        // 42
    println(factorial(5))       // 120
    println(fib(10))            // 55
}
```

---

## Using the Math Library

```ros
import math

fn main() {
    println(abs(-99))               // 99
    println(clamp(200, 0, 100))     // 100
    println(isqrt(144))             // 12
    println(ipow(2, 8))             // 256
    println(lerp(0, 100, 3, 10))    // 30  (30% of 100)
    println(map_range(50, 0, 100, 0, 255))  // 127
}
```

---

## Simple GUI Window

```ros
import gui

fn main() {
    let win: i32 = gui_window(100, 80, 320, 200, "Hello GUI")
    let mut running: i32 = 1

    while running == 1 {
        let ms:  i32 = gui_mouse(win)
        let key: i32 = gui_poll(win)

        gui_fill(win, COL_BG())
        gui_header(win, 10, 8, 300, "Hello from .ros!")

        gui_label(win, 10, 50, "Press Q or close to quit.", COL_SUBTEXT())

        gui_flush(win)

        if key == EV_CLOSE() { running = 0 }
        if key == 113        { running = 0 }
    }

    gui_close(win)
}
```

---

## Interactive GUI with Widgets

```ros
import gui

fn main() {
    let win: i32 = gui_window(60, 40, 420, 300, "Widget Demo")
    let mut running: i32 = 1
    let mut count:   i32 = 0
    let mut checked: i32 = 0
    let mut slider:  i32 = 50

    while running == 1 {
        let ms:  i32 = gui_mouse(win)
        let key: i32 = gui_poll(win)

        gui_fill(win, COL_BG())

        // Header
        gui_header(win, 8, 8, 404, "Widget Demo")
        gui_hsep(win, 8, 34, 404)

        // Button
        if gui_btn(win, 8, 44, 120, 26, "Increment", ms) == 1 {
            count += 1
        }

        // Stat card
        gui_stat_card(win, 140, 44, 100, 46, io_itoa(0, count), "Clicks", COL_ACCENT())

        // Checkbox
        if gui_checkbox(win, 8, 104, "Enable feature", checked, ms) == 1 {
            if checked == 0 { checked = 1 } else { checked = 0 }
        }

        // Slider + progress
        gui_label(win, 8, 132, "Volume:", COL_SUBTEXT())
        slider = gui_slider(win, 70, 128, 340, slider, 0, 100, ms)
        gui_progress(win, 8, 150, 404, 12, slider, 100, COL_ACCENT())

        // Tags
        gui_tag(win, 8,  172, "active", COL_SUCCESS(), COL_WHITE())
        gui_tag(win, 68, 172, "v1.0",   COL_ACCENT(),  COL_WHITE())

        // Notify
        if checked == 1 {
            gui_notify(win, 8, 200, 404, "Feature enabled!", 3)
        }

        gui_flush(win)

        if key == EV_CLOSE() { running = 0 }
        if key == 113        { running = 0 }
    }

    gui_close(win)
}
```

---

## File I/O with io Library

```ros
import io

fn main() {
    // Write a file
    let tb: i32 = io_open("/home/test.txt")
    io_key(tb, 72)   // 'H'
    io_key(tb, 101)  // 'e'
    io_key(tb, 108)  // 'l'
    io_key(tb, 108)  // 'l'
    io_key(tb, 111)  // 'o'
    io_save(tb)
    io_close(tb)

    // Read it back
    let r: i32 = io_open("/home/test.txt")
    println(io_getline(r, 0))   // "Hello"
    io_close(r)
}
```

---

## Language Test Suite

Generate the full feature test with `sample test`. It tests:
arithmetic, variables, compound assignment, comparisons, logical/bitwise operators,
if/else-if/else chains, while loops, for loops, break/continue, functions, recursion,
and nested calls — printing a `PASS / FAIL` summary at the end.
