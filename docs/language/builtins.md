# .ros Language — Built-in Functions

Built-in functions are always available without any `import`.

---

## Console Output

### `print(x)`

Prints an integer or string literal to the console without a trailing newline.

```ros
print("Count: ")
print(42)
print("\n")
```

- If `x` is a `str` literal → writes the string bytes (honours `\n`, `\t`, etc.)
- If `x` is an `i32`/`u32` expression → converts to decimal ASCII and writes it

### `println(x)`

Equivalent to `print(x)` followed by a newline character.

```ros
println("Hello, RandomOS!")   // "Hello, RandomOS!\n"
println(99)                   // "99\n"
```

---

## Command-Line Arguments

### `getarg(n: i32) -> i32`

Returns the n-th command-line argument as a string handle (an `i32` that encodes
a pointer to the argument string).

```ros
let fname: i32 = getarg(1)   // first user argument (program name = index 0)
if fname != 0 {
    println(fname)           // print() accepts the handle as a str-like value
}
```

Arguments are passed from the shell: `./myprogram.rox foo bar`  
`getarg(0)` = `"./myprogram.rox"`, `getarg(1)` = `"foo"`, `getarg(2)` = `"bar"`.
Returns `0` if the index is out of range.

---

## GUI Syscalls

GUI functions are available when `import gui` is at the top of the file. They
map directly to kernel GUI syscalls via `int 0x80`.

### Window Management

| Function | Signature | Description |
|----------|-----------|-------------|
| `gui_window` | `(x y w h title: str) -> i32` | Create a new window; returns window handle |
| `gui_close` | `(win: i32)` | Close and destroy the window |
| `gui_flush` | `(win: i32)` | Blit back-buffer to screen (call once per frame) |

### Event Handling

| Function | Signature | Description |
|----------|-----------|-------------|
| `gui_poll` | `(win: i32) -> i32` | Non-blocking: return next key code or 0 |
| `gui_wait` | `(win: i32) -> i32` | Blocking: wait for next key event |
| `gui_mouse` | `(win: i32) -> i32` | Return current mouse state as packed `i32` |
| `EV_CLOSE` | `() -> i32` | Return the close-button event constant |

### Drawing

| Function | Signature | Description |
|----------|-----------|-------------|
| `gui_fill` | `(win color: i32)` | Fill entire window with color |
| `gui_fill_rect` | `(win x y w h color: i32)` | Filled rectangle |
| `gui_rect` | `(win x y w h: i32)` | Outline rectangle (current pen) |
| `gui_line` | `(win x0 y0 x1 y1: i32)` | Draw line (current pen) |
| `gui_circle` | `(win cx cy r color: i32)` | Outline circle |
| `gui_fill_circle` | `(win cx cy r color: i32)` | Filled circle |
| `gui_rrect` | `(win x y w h radius color: i32)` | Filled rounded rectangle |
| `gui_text` | `(win x y text: str color: i32)` | Draw text string |
| `gui_pen` | `(win color: i32)` | Set current pen color |

### Mouse State Helpers

| Function | Signature | Description |
|----------|-----------|-------------|
| `ui_mx` | `(ms: i32) -> i32` | Extract mouse X from packed state |
| `ui_my` | `(ms: i32) -> i32` | Extract mouse Y from packed state |
| `ui_hover` | `(ms x y w h: i32) -> i32` | 1 if mouse is over the rectangle |
| `ui_click` | `(ms x y w h: i32) -> i32` | 1 if left button clicked inside rectangle |

---

## Text Buffer (`tbuf_*` — used via `io.ros`)

These are low-level syscall wrappers exposed as `io_*` helpers through `import io`.
Direct use is possible but the `io.ros` wrappers are preferred.

| Syscall | Description |
|---------|-------------|
| `tbuf_open(path: str) -> i32` | Open or create a text buffer backed by a VFS file |
| `tbuf_close(h: i32)` | Close and discard the text buffer |
| `tbuf_save(h: i32)` | Flush the buffer to its VFS file |
| `tbuf_getline(h n: i32) -> i32` | Return line `n` as a string handle |
| `tbuf_linecount(h: i32) -> i32` | Number of lines in the buffer |
| `tbuf_cursor(h: i32) -> i32` | Packed cursor: `line \| (col << 16)` |
| `tbuf_input(h key: i32)` | Feed a key code to the buffer (moves cursor, inserts/deletes) |
| `tbuf_numstr(h n: i32) -> i32` | Format integer `n` as a string handle (for line numbers) |

---

## Spawn

| Function | Description |
|----------|-------------|
| `spawn_term()` | Open a new GUI terminal window (requires GUI mode, used by `term.rox`) |
