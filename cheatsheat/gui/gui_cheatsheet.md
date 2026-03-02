# GUI Mode — Quick Reference

Enable GUI mode: `cmake -DGUI_MODE=ON ..`

---

## Layer Stack

```
.ros program
    │  int 0x80 / gui_* syscalls
    ▼
gui_init.c  →  desktop.c  →  wm.c  →  gfx.c  →  fb.c  →  VESA framebuffer
                                        font.c
                mouse.c (IRQ12)
```

---

## Core Window API

```ros
import gui

let win: i32 = gui_window(x, y, w, h, "Title")  // create window, returns handle
gui_fill(win, color)                              // fill entire window
gui_flush(win)                                    // blit back-buffer to screen (call once/frame)
gui_close(win)                                    // destroy window
```

---

## Event API

```ros
let key: i32 = gui_poll(win)     // non-blocking: next key or 0
let key: i32 = gui_wait(win)     // blocking: wait for key
let ms:  i32 = gui_mouse(win)    // packed mouse state

// Unpack mouse state
let mx: i32 = ui_mx(ms)
let my: i32 = ui_my(ms)

// Hit testing
ui_hover(ms, x, y, w, h)   // 1 if mouse over rect
ui_click(ms, x, y, w, h)   // 1 if left-click inside rect

// Event constants
EV_CLOSE()   // returned when window close button is clicked
```

---

## Drawing Primitives

```ros
gui_fill(win, color)                         // clear to color
gui_fill_rect(win, x, y, w, h, color)        // filled rectangle
gui_rect(win, x, y, w, h)                    // outline rectangle (current pen)
gui_pen(win, color)                          // set pen color
gui_line(win, x0, y0, x1, y1)               // line
gui_circle(win, cx, cy, r, color)            // outline circle
gui_fill_circle(win, cx, cy, r, color)       // filled circle
gui_rrect(win, x, y, w, h, radius, color)    // filled rounded rectangle
gui_text(win, x, y, "text", color)           // draw string (8×16 font)
```

---

## Color Constants (from gui.ros)

| Constant | Typical Use |
|----------|-------------|
| `COL_BG()` | Window background |
| `COL_SURFACE()` | Panel / elevated surface |
| `COL_CARD()` | Input / card background |
| `COL_TEXT()` | Primary text |
| `COL_SUBTEXT()` | Muted / secondary text |
| `COL_BORDER()` | Borders, separators |
| `COL_ACCENT()` | Primary blue accent |
| `COL_HOVER()` | Hover highlight |
| `COL_PRESS()` | Click highlight |
| `COL_WARN()` | Warning amber |
| `COL_DANGER()` | Error red |
| `COL_SUCCESS()` | Success green |
| `COL_WHITE()` | Pure white |
| `COL_BLACK()` | Pure black |
| `COL_DGRAY()` | Dark grey |

Raw color: `0xRRGGBB` as `i32`, e.g. `0xFF4444` for red.

---

## Layout Helpers (from gui.ros)

### Grid layout
```ros
let cw: i32 = gcell_w(avail, cols, gap)     // column width
let ch: i32 = gcell_h(avail, rows, gap)     // row height
let cx: i32 = gcell_x(ox, avail, cols, gap, col_index)
let cy: i32 = gcell_y(oy, avail, rows, gap, row_index)
```

### Flex (equal-width) layout
```ros
let iw: i32 = flex_w(avail, n, gap)
let ix: i32 = flex_x(ox, avail, n, gap, item_index)
```

---

## Widget Quick Reference (from gui.ros)

```ros
// Structure
gui_panel(win, x, y, w, h, color)
gui_header(win, x, y, w, "Title")
gui_hsep(win, x, y, w)   gui_vsep(win, x, y, h)
gui_divider(win, x, y, w, "label")

// Text
gui_label(win, x, y, "text", color)

// Buttons — return 1 on click
gui_btn(win, x, y, w, h, "label", ms)
gui_btn_outline(win, x, y, w, h, "label", ms)
gui_btn_danger(win, x, y, w, h, "label", ms)
gui_btn_success(win, x, y, w, h, "label", ms)

// Toggle inputs — return 1 when state changes
gui_checkbox(win, x, y, "label", checked, ms)
gui_radio(win, x, y, "label", selected, ms)
gui_toggle(win, x, y, "label", on, ms)

// Slider — returns new value
gui_slider(win, x, y, w, val, min, max, ms)

// Displays
gui_progress(win, x, y, w, h, val, maxv, color)
gui_scrollbar(win, x, y, h, pos, maxp)
gui_stat_card(win, x, y, w, h, "value", "label", color)
gui_table_row(win, x, y, w, h, even)

// Overlay
gui_tooltip(win, x, y, "text")
gui_notify(win, x, y, w, "text", kind)  // kind: 0=info 1=warn 2=error 3=success

// Tags & badges
gui_tag(win, x, y, "text", bg_color, fg_color)
gui_badge(win, cx, cy, r, color)
gui_icon(win, x, y, size, color)

// Textbox (visual only — key handling in user code)
gui_textbox(win, x, y, w, h, "text", focused)
```

---

## Framebuffer Details

| Property | Value |
|----------|-------|
| Resolution | 1024×768 (GRUB-requested, configurable) |
| Pixel format | `0x00RRGGBB` packed in `uint32_t` |
| Buffering | Double-buffered (back → front on `gui_flush`) |
| Font | 256-glyph 8×16 VGA bitmap (embedded) |

---

## Shell Commands (GUI mode)

```
mode gui                        # switch to GUI desktop
mode nerd                       # switch back to text shell
microui                         # launch microui demo window
desktop add myapp               # add /bin/myapp.rox as desktop icon
desktop delete myapp            # remove icon by label
setup                           # install rxt editor + term spawner
```
