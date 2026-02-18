# VGA Text Framebuffer Cheatsheet (ports & cell format)

## Memory
- Base: 0xB8000 (text mode, color)
- Cell size: 2 bytes (char + attribute)

## Cell layout (per 16-bit word)
- Bits 15–8: ASCII character
- Bits 7–4 : Foreground color (0–15)
- Bits 3–0 : Background color (0–15)

## Color values
0 Black, 1 Blue, 2 Green, 3 Cyan, 4 Red, 5 Magenta, 6 Brown, 7 Light Grey,
8 Dark Grey, 9 Light Blue, 10 Light Green, 11 Light Cyan,
12 Light Red, 13 Light Magenta, 14 Light Brown, 15 White

## Ports (cursor control)
- Command: 0x3D4
- Data   : 0x3D5

Command register index values:
- 0x0E: high byte of cursor position
- 0x0F: low byte of cursor position

## Set cursor position (row, col)
1) pos = row * 80 + col
2) out 0x3D4, 0x0E; out 0x3D5, (pos >> 8) & 0xFF
3) out 0x3D4, 0x0F; out 0x3D5, pos & 0xFF

## Write a cell (example)
- Put 'A' (0x41) with fg=Green (2), bg=DarkGrey (8) at cell 0:
  - word = 0x4128; store at 0xB8000

## Clear screen pattern
- Fill 80*25 cells with 0x20 (space) + default attribute (e.g., fg=LightGrey, bg=Black → 0x0720)
