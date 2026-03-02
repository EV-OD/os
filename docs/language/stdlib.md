# .ros Standard Library Reference

The standard library is written to `/lib/ros/` at every boot by `shell_init_stdlib()`.
Use `import <name>` to include a module.

Generated API docs: run `python3 compiler/docs_gen/docs_gen.py --all-stdlib` to produce
up-to-date per-function Markdown from the live `.ros` source.

---

## `import math` — `/lib/ros/math.ros`

Integer math utilities.

| Function | Signature | Description |
|----------|-----------|-------------|
| `abs` | `(x: i32) -> i32` | Absolute value |
| `min` | `(a b: i32) -> i32` | Minimum of two values |
| `max` | `(a b: i32) -> i32` | Maximum of two values |
| `clamp` | `(v lo hi: i32) -> i32` | Clamp `v` to `[lo, hi]` |
| `isqrt` | `(n: i32) -> i32` | Integer square root (Babylonian method) |
| `ipow` | `(base exp: i32) -> i32` | Integer exponentiation (fast binary method) |
| `lerp` | `(a b t scale: i32) -> i32` | Linear interpolation: `a + (b-a)*t/scale` |
| `map_range` | `(v in_min in_max out_min out_max: i32) -> i32` | Re-map value across two ranges |

**Usage:**
```ros
import math

let r: i32 = isqrt(144)        // 12
let p: i32 = ipow(2, 10)       // 1024
let v: i32 = clamp(200, 0, 100) // 100
let m: i32 = map_range(50, 0, 100, 0, 255) // 127
```

---

## `import io` — `/lib/ros/io.ros`

Text-buffer file I/O (wrappers over `tbuf_*` syscalls).

| Function | Signature | Description |
|----------|-----------|-------------|
| `io_open` | `(path: str) -> i32` | Open/create file; returns buffer handle `h` |
| `io_close` | `(h: i32)` | Close buffer (does not save) |
| `io_save` | `(h: i32)` | Flush buffer to VFS file |
| `io_getline` | `(h n: i32) -> i32` | Return line number `n` as a string handle |
| `io_key` | `(h key: i32)` | Feed key event to buffer |
| `io_lines` | `(h: i32) -> i32` | Total number of lines |
| `io_cursor` | `(h: i32) -> i32` | Packed cursor: low 16 bits = line, high 16 = col |
| `io_cline` | `(h: i32) -> i32` | Current line number |
| `io_ccol` | `(h: i32) -> i32` | Current column number |
| `io_itoa` | `(h n: i32) -> i32` | Format integer as string handle |
| `print_sep` | `()` | Print `"---…---\n"` separator |
| `print_header` | `(title: str)` | Print separator + title + separator |

**Usage:**
```ros
import io

let tb: i32 = io_open("/home/notes.txt")
let n: i32  = io_lines(tb)

let mut i: i32 = 0
while i < n {
    println(io_getline(tb, i))
    i += 1
}

io_close(tb)
```

---

## `import gui` — `/lib/ros/gui.ros`

Full GUI widget toolkit. Requires GUI mode (`-DGUI_MODE=ON`).

### Color Constants

| Constant | Description |
|----------|-------------|
| `COL_BG()` | Window background |
| `COL_SURFACE()` | Elevated surface (panel) |
| `COL_CARD()` | Card / input background |
| `COL_TEXT()` | Primary text |
| `COL_SUBTEXT()` | Secondary / muted text |
| `COL_BORDER()` | Border / separator |
| `COL_ACCENT()` | Primary accent (blue) |
| `COL_HOVER()` | Hover highlight |
| `COL_PRESS()` | Click highlight |
| `COL_WARN()` | Warning (amber) |
| `COL_DANGER()` | Error / danger (red) |
| `COL_SUCCESS()` | Success (green) |
| `COL_BLUE()` | Blue |
| `COL_GREEN()` | Green |
| `COL_DGRAY()` | Dark grey |
| `COL_WHITE()` | White |
| `COL_BLACK()` | Black |
| `EV_CLOSE()` | Window close button event code |

---

### Layout Helpers

#### Grid Layout — `gcell_*`

```ros
// Column width / height given total available space, column/row count, gap size
fn gcell_w(avail cols gap: i32) -> i32
fn gcell_h(avail rows gap: i32) -> i32

// X / Y position of grid cell (c=column index, r=row index)
fn gcell_x(ox avail cols gap c: i32) -> i32
fn gcell_y(oy avail rows gap r: i32) -> i32
```

#### Flex (equal-width) Layout — `flex_*`

```ros
fn flex_w(avail n gap: i32) -> i32           // equal item width
fn flex_x(ox avail n gap i: i32) -> i32      // X position of item i
```

---

### Widgets

All widget functions take `win: i32` as the first parameter (window handle).

#### Containers / Structure

| Function | Signature | Description |
|----------|-----------|-------------|
| `gui_panel` | `(win x y w h color: i32)` | Filled rounded-rect panel |
| `gui_header` | `(win x y w: i32, text: str)` | Accent-colored title bar + text |
| `gui_hsep` | `(win x y w: i32)` | Horizontal separator line |
| `gui_vsep` | `(win x y h: i32)` | Vertical separator line |
| `gui_divider` | `(win x y w: i32, text: str)` | Separator line with side label |

#### Text & Labels

| Function | Signature | Description |
|----------|-----------|-------------|
| `gui_label` | `(win x y: i32, text: str, fg: i32)` | Plain text label |

#### Buttons

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `gui_btn` | `(win x y w h: i32, label: str, ms: i32)` | `1` if clicked | Filled accent button |
| `gui_btn_outline` | `(win x y w h: i32, label: str, ms: i32)` | `1` if clicked | Ghost / outline button |
| `gui_btn_danger` | `(win x y w h: i32, label: str, ms: i32)` | `1` if clicked | Red danger button |
| `gui_btn_success` | `(win x y w h: i32, label: str, ms: i32)` | `1` if clicked | Green success button |

#### Inputs

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `gui_checkbox` | `(win x y: i32, label: str, checked ms: i32)` | `1` if toggled | Checkbox with label |
| `gui_radio` | `(win x y: i32, label: str, selected ms: i32)` | `1` if clicked | Radio button |
| `gui_toggle` | `(win x y: i32, label: str, on ms: i32)` | `1` if toggled | Toggle switch |
| `gui_slider` | `(win x y w val min max ms: i32)` | new value | Horizontal slider |
| `gui_textbox` | `(win x y w h: i32, text: str, focused: i32)` | — | Text input visual |

#### Displays

| Function | Signature | Description |
|----------|-----------|-------------|
| `gui_progress` | `(win x y w h val maxv color: i32)` | Progress bar |
| `gui_scrollbar` | `(win x y h pos maxp: i32)` | Vertical scrollbar indicator |
| `gui_stat_card` | `(win x y w h: i32, value label: str, color: i32)` | Metric card with accent strip |
| `gui_table_row` | `(win x y w h even: i32)` | Alternating table row background |
| `gui_badge` | `(win cx cy r color: i32)` | Circular badge |
| `gui_tag` | `(win x y: i32, text: str, bg fg: i32)` | Pill-shaped tag / chip |
| `gui_icon` | `(win x y size color: i32)` | Square icon box |

#### Overlays

| Function | Signature | Description |
|----------|-----------|-------------|
| `gui_tooltip` | `(win x y: i32, text: str)` | Tooltip above the given point |
| `gui_notify` | `(win x y w: i32, text: str, kind: i32)` | Notification bar (`0`=info `1`=warn `2`=error `3`=success) |

---

### Typical GUI Frame Loop

```ros
import gui

fn main() {
    let win: i32 = gui_window(50, 50, 400, 300, "Demo")
    let mut running: i32 = 1
    let mut counter: i32 = 0

    while running == 1 {
        let ms:  i32 = gui_mouse(win)
        let key: i32 = gui_poll(win)

        // -- Draw --------------------------------------------------------
        gui_fill(win, COL_BG())

        gui_header(win, 10, 10, 380, "My App")

        if gui_btn(win, 10, 50, 120, 28, "Click me", ms) == 1 {
            counter += 1
        }

        gui_label(win, 10, 90, "Count:", COL_SUBTEXT())
        gui_label(win, 70, 90, io_itoa(0, counter), COL_TEXT())

        gui_flush(win)

        // -- Events ------------------------------------------------------
        if key == EV_CLOSE() { running = 0 }
        if key == 113        { running = 0 }   // q
    }

    gui_close(win)
}
```
