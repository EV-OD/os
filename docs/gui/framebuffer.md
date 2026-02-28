# Framebuffer — VESA / Multiboot Details

## 1  Requesting a Framebuffer from GRUB

GRUB (Multiboot 1) lets the kernel request a linear framebuffer by setting
**bit 2** (`MULTIBOOT_VIDEO_MODE`) in the multiboot header flags word and
appending the desired mode parameters after the checksum.

### Modified `loader.s` header block

```nasm
MAGIC_NUMBER equ 0x1BADB002
FLAGS        equ 0x00000007   ; ALIGN | MEM_INFO | VIDEO_MODE
CHECKSUM     equ -(MAGIC_NUMBER + FLAGS)

    dd MAGIC_NUMBER
    dd FLAGS
    dd CHECKSUM
    ; address fields (only used when bit 16 is set – we leave them zero)
    dd 0, 0, 0, 0, 0
    ; video mode request
    dd 0      ; mode_type  0 = linear RGB framebuffer
    dd 1024   ; width
    dd 768    ; height
    dd 32     ; depth (bits per pixel)
```

GRUB treats these as *hints*: it tries to satisfy the request but may
choose a different resolution.  Always read back the actual values from
`multiboot_info_t::framebuffer_*` at runtime.

---

## 2  Multiboot Framebuffer Fields

| Field | Type | Meaning |
|-------|------|---------|
| `framebuffer_addr` | `uint64_t` | Physical base address of the pixel buffer |
| `framebuffer_pitch` | `uint32_t` | Bytes per scanline (may be > width × bpp/8) |
| `framebuffer_width` | `uint32_t` | Pixels per row |
| `framebuffer_height` | `uint32_t` | Rows |
| `framebuffer_bpp` | `uint8_t` | Bits per pixel (32 requested, 24 possible) |
| `framebuffer_type` | `uint8_t` | 0 = indexed, **1 = RGB**, 2 = EGA text |

Guard: `mb->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO` must be set.

For RGB mode (type 1) additional fields describe the per-channel layout:

| Field | Meaning |
|-------|---------|
| `framebuffer_red_field_position` | Bit offset of the red channel (e.g. 16) |
| `framebuffer_red_mask_size` | Number of red bits (e.g. 8) |
| `framebuffer_green_field_position` / `_mask_size` | ditto for green |
| `framebuffer_blue_field_position` / `_mask_size` | ditto for blue |

---

## 3  Pixel Address Formula

```
pixel_addr = framebuffer_addr + (y * pitch) + (x * (bpp / 8))
```

For 32 bpp the pixel value is a 32-bit little-endian word packed as
`0x00RRGGBB` when `red_position=16, green_position=8, blue_position=0`
(the most common VESA layout).

---

## 4  Higher-Half Mapping

The framebuffer physical address is usually above 0xE0000000.  The kernel's
page tables only cover `[PHYS 0, RAM_TOP)` mapped at `[0xC0000000, …)`.
`fb_init()` calls `paging_map_mmio()` to add a dedicated mapping for the
framebuffer region before any pixel is written.

---

## 5  Back-Buffer (Double Buffering)

To eliminate tearing, `fb.c` maintains a shadow buffer allocated from the
kernel heap:

```
front buffer  – the VESA linear frame (MMIO, write-only)
back  buffer  – kmalloc'd, width × height × 4 bytes
```

All drawing goes to the back buffer.  `fb_flush()` (or `fb_flush_rect()`)
copies the dirty region to the front buffer with a `memcpy`.

---

## 6  Supported BPP Modes

| BPP | Status | Notes |
|-----|--------|-------|
| 32 | **Implemented** | Pixel = `uint32_t`, fastest path |
| 24 | Planned | 3-byte writes, no alignment guarantee |
| 16 | Planned | RGB 5-6-5 packed |
