# Window Manager

## Design Goals

- **Simplicity first**: no compositor, no GPU acceleration.  All blitting is
  software on the CPU using `memcpy` / plain C loops.
- **Fixed-size allocations**: `wm_stack[]` holds at most `WM_MAX_WINDOWS` (16)
  descriptors.  No dynamic pointer chasing during the paint loop.
- **Direct-draw model**: each window gets its own `canvas` (a `kmalloc`'d
  32-bit-per-pixel buffer).  The WM composes windows onto the global frame
  back-buffer at flush time.

---

## Window Lifecycle

```
wm_create()   → allocates wm_window_t + canvas, pushes to front of stack
  │
  └─ on_paint() callback ← called whenever the window is marked dirty
       │
       └─ wm_paint()     → blits canvas into fb back-buffer with title bar
            │
            └─ fb_flush_rect() → copies dirty region to VESA front buffer
```

---

## Z-Order Rules

`wm_stack[0]` is the **top-most** (focused) window.
`wm_stack[wm_count-1]` is the **bottom-most** window (behind everything).

Compositing iterates from **high index → 0** so the focused window is drawn
last and therefore appears on top after `fb_flush`.

`wm_raise(win)` swaps the target into slot 0, shifting others up by one.

---

## Title Bar Layout

```
┌─────────────────────────────────────── [X] ┐
│  Title text                                  │
├──────────────────────────────────────────────┤
│                                              │
│  canvas area  (w × (h - TITLE_H) pixels)    │
│                                              │
└──────────────────────────────────────────────┘
```

| Metric | Value |
|--------|-------|
| `TITLE_H` | 20 px |
| Title bg (focused) | `COLOR_WINDOW_TITLE` |
| Title bg (unfocused) | `COLOR_DKGRAY` |
| Close button | red square, 14×14, top-right corner |

---

## Event Dispatch

### Keyboard

`wm_dispatch_key(c)` forwards the character to `wm_stack[0]->on_key`.

### Mouse

`wm_dispatch_mouse(mx, my, buttons)` first detects hit-testing:

1. If the mouse is in the title bar and button 0 is pressed → start **drag**.
2. If the mouse is in the close button and button 0 released → `wm_destroy`.
3. Otherwise → forward to the focused window's `on_mouse` callback.

Drag state is stored in two static integers `drag_dx`, `drag_dy` that
accumulate delta movement until button 0 is released.

---

## Painting a Window

```c
void wm_paint(wm_window_t *win) {
    // 1. Title bar
    gfx_fill_rect(win->x, win->y, win->w, TITLE_H,
                  (win == wm_focused()) ? COLOR_WINDOW_TITLE : COLOR_DKGRAY);
    gfx_draw_text(win->x + 4, win->y + 2, win->title,
                  COLOR_WHITE, COLOR_TRANSPARENT);

    // 2. Canvas blit
    gfx_blit(win->x, win->y + TITLE_H,
             win->w, win->h - TITLE_H,
             win->canvas, win->w);

    // 3. Border
    gfx_draw_rect(win->x, win->y, win->w, win->h, COLOR_DKGRAY);
}
```
