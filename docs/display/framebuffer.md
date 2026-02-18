# Framebuffer Format

## Bit Layout

| Bits | 15-8 | 7-4 | 3-0 |
|------|------|-----|-----|
| Content | ASCII | FG | BG |

## Color Palette

| Color | Value | Color | Value | Color | Value | Color | Value |
|-------|-------|-------|-------|-------|-------|-------|-------|
| Black | 0 | Red | 4 | Dark grey | 8 | Light red | 12 |
| Blue | 1 | Magenta | 5 | Light blue | 9 | Light magenta | 13 |
| Green | 2 | Brown | 6 | Light green | 10 | Light brown | 14 |
| Cyan | 3 | Light grey | 7 | Light cyan | 11 | White | 15 |


To write the character A with a green foreground (2) and dark grey background (8) at place (0,0), the following assembly code instruction is used:
```asm
mov [0x000B8000], 0x4128
```

## Moving the Cursor
Port 0x3D4: port that describes the data
Port 0x3D5: port that contains the data

To set the cursor at row one, column zero (position 80 = 0x0050), one would use the following assembly code instructions:

```asm
out 0x3D4, 14      ; 14 tells the framebuffer to expect the highest 8 bits of the position
out 0x3D5, 0x00    ; sending the highest 8 bits of 0x0050
out 0x3D4, 15      ; 15 tells the framebuffer to expect the lowest 8 bits of the position
out 0x3D5, 0x50    ; sending the lowest 8 bits of 0x0050
```



