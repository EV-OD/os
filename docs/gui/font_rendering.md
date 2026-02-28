# Font Rendering

## Glyph Data Layout

The embedded font is the classic **VGA 8×16** bitmap font (same glyphs used in
EGA/VGA text mode).  Each glyph is stored as 16 bytes — one byte per row,
top-to-bottom.

```
Byte 0 → row 0 (top)
Byte 1 → row 1
...
Byte 15 → row 15 (bottom)

Within each byte:
  bit 7 = leftmost pixel (x+0)
  bit 6 = x+1
  ...
  bit 0 = rightmost pixel (x+7)
```

The full table is `gui_font[256]` giving 256 × 16 = 4096 bytes of read-only
font data embedded in the kernel image.

---

## Rendering Algorithm

```c
void font_draw_char(int x, int y, char c,
                    color_t fg, color_t bg)
{
    const glyph_t *g = &gui_font[(unsigned char)c];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = g->rows[row];
        for (int col = 0; col < 8; col++) {
            color_t px = (bits & (0x80 >> col)) ? fg : bg;
            fb_put_pixel(x + col, y + row, px);
        }
    }
}
```

Transparent background: pass `COLOR_TRANSPARENT` (defined as `0xFF000000`)
as `bg`; the drawing loop skips `fb_put_pixel` for background pixels.

---

## String Rendering

`font_draw_str` simply calls `font_draw_char` for each character, advancing
`x` by `FONT_W` (8) each time.  A `\n` advances `y` by `FONT_H` (16) and
resets `x` to the left margin.

---

## Constants

| Constant | Value | Meaning |
|----------|-------|---------|
| `FONT_W` | 8 | Glyph width in pixels |
| `FONT_H` | 16 | Glyph height in pixels |
| `COLOR_TRANSPARENT` | `0xFF000000` | Skip pixel when used as bg colour |
