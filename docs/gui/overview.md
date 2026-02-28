# GUI Mode — Overview

RabinOS ships with two display personalities:

| Mode | Description |
|------|-------------|
| **nerd mode** (default) | Classic 80×25 VGA text terminal, no paging needed |
| **GUI mode** | VESA linear framebuffer, pixel-level graphics, windowed desktop |

Both modes implement the same `terminal_t` vtable so the shell and every
`.rox` process work unchanged regardless of which mode is active.

---

## Layer Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                         kmain.c                             │
│  kernel_init() → gui_init() → desktop_run()  /  shell_run() │
└────────────────────────┬────────────────────────────────────┘
                         │ GUI mode
          ┌──────────────▼──────────────┐
          │       desktop.c             │  ← taskbar, icons, wallpaper
          └──────────────┬──────────────┘
                         │
          ┌──────────────▼──────────────┐
          │         wm.c                │  ← window list, focus, events
          └──────────────┬──────────────┘
                         │
     ┌───────────────────┼──────────────────────┐
     │                   │                      │
┌────▼──────┐   ┌────────▼──────┐   ┌──────────▼───┐
│  gfx.c    │   │  gui_term.c   │   │   mouse.c    │
│ primitives│   │terminal_t impl│   │  PS/2 driver │
└────┬──────┘   └───────────────┘   └──────────────┘
     │
┌────▼──────┐
│  font.c   │  ← embedded 8×16 bitmap font
└────┬──────┘
     │
┌────▼──────┐
│   fb.c    │  ← raw framebuffer, put_pixel, blit
└───────────┘
     ▲
     │  GRUB VESA linear framebuffer  (multiboot_info_t::framebuffer_*)
```

---

## Implementation Steps

| Step | File(s) | What it adds |
|------|---------|--------------|
| 1 | `asm/loader.s` | Request VESA framebuffer from GRUB (multiboot flags) |
| 2 | `gui/fb.h`, `gui/fb.c` | Low-level framebuffer access (init, put_pixel, blit) |
| 3 | `gui/color.h` | `color_t` type and named RGB constants |
| 4 | `gui/font.h`, `gui/font.c` | Embedded 8×16 VGA bitmap font, text rendering |
| 5 | `gui/gfx.h`, `gui/gfx.c` | Rectangles, lines, circles, filled shapes |
| 6 | `gui/mouse.h`, `gui/mouse.c` | PS/2 mouse driver, cursor rendering |
| 7 | `gui/wm.h`, `gui/wm.c` | Window list, Z-order, focus, event dispatch |
| 8 | `gui/desktop.h`, `gui/desktop.c` | Desktop, taskbar, icon grid, wallpaper |
| 9 | `gui/gui_term.h`, `gui/gui_term.c` | `terminal_t` backed by framebuffer |
| 10 | `gui/gui_init.h`, `gui/gui_init.c` | One-shot orchestrator called from `kmain` |

---

## How to Enable GUI Mode

GUI mode is selected at compile time via the `GUI_MODE` CMake option
(default `OFF`).  When `ON`, `loader.s` requests a 1024×768×32 VESA
framebuffer from GRUB, `kernel_init` initialises the GUI subsystem, and
`kmain` calls `desktop_run()` instead of `shell_run()`.

```bash
cmake -DGUI_MODE=ON ..
make
```

At runtime the kernel inspects the `framebuffer_type` field handed back by
GRUB.  If GRUB could not satisfy the request and fell back to text-mode the
kernel gracefully demotes itself to nerd mode.

---

## Files Reference

```
c_files/
  includes/gui/
    fb.h          framebuffer abstraction
    color.h       color_t and RGB constants
    font.h        bitmap font rendering
    gfx.h         drawing primitives
    mouse.h       PS/2 mouse driver
    wm.h          window manager
    desktop.h     desktop environment
    gui_term.h    terminal_t implementation
    gui_init.h    GUI subsystem orchestrator

  src/gui/
    fb.c
    font.c
    gfx.c
    mouse.c
    wm.c
    desktop.c
    gui_term.c
    gui_init.c

docs/gui/
    overview.md         ← you are here
    architecture.md     deep-dive into data structures
    framebuffer.md      VESA / multiboot framebuffer details
    font_rendering.md   glyph data layout and rendering algorithm
    window_manager.md   WM event model and Z-order rules
    mouse.md            PS/2 mouse protocol and cursor rendering
```
