# GUI Architecture — Data Structures & Event Model

## 1  Module Dependency Order

```
fb              ← no dependencies (only multiboot info + paging_map_mmio)
color           ← header-only (no .c)
font            ← depends on fb
gfx             ← depends on fb, color, font
mouse           ← depends on fb, gfx (cursor sprite)
wm              ← depends on gfx, mouse, kheap
desktop         ← depends on wm, gfx, font
gui_term        ← depends on gfx, font, keyboard, kheap
gui_init        ← depends on all of the above
```

---

## 2  Framebuffer (`fb.h`)

```c
typedef struct {
    uint32_t *back;      /* kmalloc'd shadow buffer               */
    uint8_t  *front;     /* MMIO linear framebuffer (virtual addr) */
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;     /* bytes per row in the FRONT buffer      */
    uint8_t   bpp;
    /* RGB channel layout (from multiboot) */
    uint8_t   r_pos, r_bits;
    uint8_t   g_pos, g_bits;
    uint8_t   b_pos, b_bits;
} fb_t;
```

Key API:
- `fb_init(mb)`            — parse multiboot, map MMIO, alloc back buffer
- `fb_put_pixel(x, y, c)` — write to back buffer only
- `fb_flush()`             — copy entire back buffer → front
- `fb_flush_rect(x,y,w,h)` — copy a dirty rectangle

---

## 3  Color (`color.h`)

`color_t` is a plain `uint32_t` in `0x00RRGGBB` format.

```c
#define COLOR_RGB(r,g,b)   ((uint32_t)(((r)<<16)|((g)<<8)|(b)))
#define COLOR_R(c)         (((c)>>16)&0xFF)
#define COLOR_G(c)         (((c)>> 8)&0xFF)
#define COLOR_B(c)         (( (c)    )&0xFF)
```

Named constants: `COLOR_BLACK`, `COLOR_WHITE`, `COLOR_GRAY`, `COLOR_DKGRAY`,
`COLOR_RED`, `COLOR_GREEN`, `COLOR_BLUE`, `COLOR_YELLOW`, `COLOR_CYAN`,
`COLOR_MAGENTA`, `COLOR_DESKTOP_BG`, `COLOR_TASKBAR`, `COLOR_WINDOW_TITLE`.

---

## 4  Font (`font.h`)

An embedded 256-glyph × 8×16 bitmap font (VGA ROM-compatible layout).
Each glyph is 16 bytes; bit 7 of each byte = leftmost pixel.

```c
typedef struct { uint8_t rows[16]; } glyph_t;
extern const glyph_t gui_font[256];

void font_draw_char(int x, int y, char c, color_t fg, color_t bg);
void font_draw_str (int x, int y, const char *s, color_t fg, color_t bg);
```

---

## 5  Drawing (`gfx.h`)

All coordinates are in screen pixels; all writes go to the back buffer.

```c
void gfx_fill_rect(int x, int y, int w, int h, color_t c);
void gfx_draw_rect(int x, int y, int w, int h, color_t c);
void gfx_draw_line(int x0, int y0, int x1, int y1, color_t c);
void gfx_draw_circle(int cx, int cy, int r, color_t c);
void gfx_fill_circle(int cx, int cy, int r, color_t c);
void gfx_draw_text (int x, int y, const char *s, color_t fg, color_t bg);
void gfx_blit      (int dx, int dy, int w, int h,
                    const uint32_t *src, int src_stride);
```

---

## 6  Mouse (`mouse.h`)

Implements the standard PS/2 3-button mouse protocol (IRQ12 / port 0x60).

```c
typedef struct { int x, y; uint8_t buttons; } mouse_state_t;

void             mouse_init(void);
mouse_state_t    mouse_get(void);
void             mouse_draw_cursor(void);   /* called by desktop loop */
```

The hardware cursor is a 16×16 sprite blitted with transparent colour `0`.

---

## 7  Window Manager (`wm.h`)

### Window descriptor

```c
typedef struct wm_window {
    int x, y, w, h;
    char title[64];
    uint32_t *canvas;   /* per-window back buffer (kmalloc)   */
    int dirty;
    void (*on_key  )(struct wm_window*, char);
    void (*on_mouse)(struct wm_window*, int mx, int my, uint8_t btn);
    void (*on_paint)(struct wm_window*);
} wm_window_t;
```

### API

```c
wm_window_t *wm_create (int x, int y, int w, int h, const char *title);
void         wm_destroy(wm_window_t *win);
void         wm_raise  (wm_window_t *win);          /* bring to front */
void         wm_paint  (wm_window_t *win);          /* composit → back buf */
void         wm_paint_all(void);
void         wm_dispatch_key  (char c);
void         wm_dispatch_mouse(int mx, int my, uint8_t buttons);
```

### Z-order

Windows are kept in a fixed-size array `wm_stack[]` ordered front→back.
`wm_raise()` moves the target to index 0.  Painting iterates back→front so
high-index (background) windows are drawn first.

---

## 8  Desktop (`desktop.h`)

The desktop owns:

- **Wallpaper** — a gradient or solid fill drawn at startup.
- **Taskbar** — a 32 px strip at the bottom showing open windows.
- **Event loop** — `desktop_run()` spins calling:
  1. `mouse_draw_cursor()`
  2. `wm_dispatch_mouse()`  /  `wm_dispatch_key()`
  3. `wm_paint_all()`
  4. `fb_flush()`

---

## 9  GUI Terminal (`gui_term.h`)

Implements `terminal_t` so the shell runs inside a window.

```c
terminal_t *gui_term_create(wm_window_t *win);
```

Internally maintains a character grid, scrolls when the last row is
reached, processes `\n`, `\r`, `\b` and ANSI colour escapes.
IRQ1 (keyboard) feeds characters into a 256-byte circular ring buffer;
`get_char()` drains the ring.
