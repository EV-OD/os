/* =========================================================================
 * gui/fb.c – Linear framebuffer abstraction
 *
 * Wraps the VESA linear framebuffer provided by GRUB (Multiboot 1).
 * Uses a kmalloc'd shadow (back) buffer; fb_flush*() copies dirty regions
 * to the MMIO front buffer.
 * ========================================================================= */

#include "gui/fb.h"
#include "gui/color.h"
#include "multiboot.h"
#include "kheap.h"
#include "paging.h"
#include "string.h"
#include "log.h"

/* -------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------- */

typedef struct {
    unsigned int  *back;    /* kmalloc'd shadow buffer (32-bpp)           */
    unsigned char *front;   /* MMIO linear framebuffer (virtual address)  */
    unsigned int   width;
    unsigned int   height;
    unsigned int   pitch;   /* bytes per scanline in the front buffer     */
    unsigned char  bpp;

    /* RGB channel packing (from multiboot) */
    unsigned char r_pos, r_bits;
    unsigned char g_pos, g_bits;
    unsigned char b_pos, b_bits;

    int ready;              /* 1 after successful fb_init()               */
    int identity_layout;   /* 1 when r=16,g=8,b=0 – back pixel == hw pixel */
} fb_state_t;

static fb_state_t s_fb;

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/**
 * Convert a 0x00RRGGBB color_t to the packed pixel word for the actual
 * framebuffer layout (supports non-standard channel positions).
 */
static unsigned int color_to_hw(color_t c)
{
    unsigned int r = COLOR_R(c);
    unsigned int g = COLOR_G(c);
    unsigned int b = COLOR_B(c);
    return (r << s_fb.r_pos) | (g << s_fb.g_pos) | (b << s_fb.b_pos);
}

/* Clamp helpers */
static inline int fb_min(int a, int b) { return a < b ? a : b; }
static inline int fb_max(int a, int b) { return a > b ? a : b; }

/* -------------------------------------------------------------------------
 * Initialisation
 * ------------------------------------------------------------------------- */

int fb_init(multiboot_info_t *mb)
{
    /* Validate multiboot flags */
    if (!(mb->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO)) {
        log_warning("[fb] multiboot: no framebuffer info flag");
        return -1;
    }

    if (mb->framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
        log_warning("[fb] framebuffer_type=%u is not RGB (type 1)", mb->framebuffer_type);
        return -1;
    }

    /* Must be 32 bpp for this initial implementation */
    if (mb->framebuffer_bpp != 32) {
        log_warning("[fb] framebuffer_bpp=%u; only 32-bpp is currently supported",
                    mb->framebuffer_bpp);
        return -1;
    }

    unsigned int phys_lo = (unsigned int)(mb->framebuffer_addr & 0xFFFFFFFFu);

    s_fb.width  = mb->framebuffer_width;
    s_fb.height = mb->framebuffer_height;
    s_fb.pitch  = mb->framebuffer_pitch;
    s_fb.bpp    = mb->framebuffer_bpp;

    s_fb.r_pos   = mb->framebuffer_red_field_position;
    s_fb.r_bits  = mb->framebuffer_red_mask_size;
    s_fb.g_pos   = mb->framebuffer_green_field_position;
    s_fb.g_bits  = mb->framebuffer_green_mask_size;
    s_fb.b_pos   = mb->framebuffer_blue_field_position;
    s_fb.b_bits  = mb->framebuffer_blue_mask_size;

    /*
     * Map the VESA MMIO region into the kernel's virtual address space.
     * paging_map_mmio() identity-maps [phys, phys + size) at the same
     * virtual address (physical == virtual for MMIO in our simple model).
     */
    unsigned int fb_bytes = s_fb.height * s_fb.pitch;
    paging_map_mmio(phys_lo, fb_bytes);
    s_fb.front = (unsigned char *)phys_lo;

    /* Allocate the back (shadow) buffer in the kernel heap. */
    unsigned int back_bytes = s_fb.width * s_fb.height * 4u;
    s_fb.back = (unsigned int *)kmalloc(back_bytes);
    if (!s_fb.back) {
        log_error("[fb] kmalloc failed for back buffer (%u bytes)", back_bytes);
        return -1;
    }
    memset(s_fb.back, 0, back_bytes);

    /* Detect identity layout (most common: 0x00RRGGBB == HW pixel) */
    s_fb.identity_layout = (s_fb.r_pos == 16 && s_fb.r_bits == 8 &&
                            s_fb.g_pos ==  8 && s_fb.g_bits == 8 &&
                            s_fb.b_pos ==  0 && s_fb.b_bits == 8);

    s_fb.ready = 1;

    log_info("[fb] %ux%u@%ubpp pitch=%u phys=0x%x back=0x%x",
             s_fb.width, s_fb.height, s_fb.bpp, s_fb.pitch,
             phys_lo, (unsigned int)s_fb.back);
    return 0;
}

int fb_available(void)
{
    return s_fb.ready;
}

unsigned int fb_width(void)
{
    return s_fb.width;
}

unsigned int fb_height(void)
{
    return s_fb.height;
}

/* -------------------------------------------------------------------------
 * Pixel operations (back buffer)
 * ------------------------------------------------------------------------- */

void fb_put_pixel(int x, int y, color_t color)
{
    if (!s_fb.ready) return;
    if ((unsigned int)x >= s_fb.width) return;
    if ((unsigned int)y >= s_fb.height) return;
    s_fb.back[(unsigned int)y * s_fb.width + (unsigned int)x] = color;
}

color_t fb_get_pixel(int x, int y)
{
    if (!s_fb.ready) return 0;
    if ((unsigned int)x >= s_fb.width) return 0;
    if ((unsigned int)y >= s_fb.height) return 0;
    return s_fb.back[(unsigned int)y * s_fb.width + (unsigned int)x];
}

void fb_fill(color_t color)
{
    if (!s_fb.ready) return;
    unsigned int  total = s_fb.width * s_fb.height;
    unsigned int *ptr   = s_fb.back;   /* local copy – asm clobbers the reg */
    /* rep stosd writes one 32-bit dword per clock: ~4× faster than a C loop */
    __asm__ volatile (
        "rep stosl"
        : "+D" (ptr), "+c" (total)
        : "a"  (color)
        : "memory"
    );
}

void fb_blit(int dst_x, int dst_y, int w, int h,
             const unsigned int *src, int src_stride)
{
    if (!s_fb.ready || !src) return;

    for (int row = 0; row < h; row++) {
        int dy = dst_y + row;
        if (dy < 0 || (unsigned int)dy >= s_fb.height) continue;
        for (int col = 0; col < w; col++) {
            int dx = dst_x + col;
            if (dx < 0 || (unsigned int)dx >= s_fb.width) continue;
            unsigned int px = src[row * src_stride + col];
            if (px != COLOR_TRANSPARENT)
                s_fb.back[(unsigned int)dy * s_fb.width + (unsigned int)dx] = px;
        }
    }
}

/* -------------------------------------------------------------------------
 * Flushing (back buffer → VESA front buffer)
 * ------------------------------------------------------------------------- */

void fb_flush(void)
{
    if (!s_fb.ready) return;
    fb_flush_rect(0, 0, (int)s_fb.width, (int)s_fb.height);
}

void fb_flush_rect(int x, int y, int w, int h)
{
    if (!s_fb.ready) return;

    int x0 = fb_max(x, 0);
    int y0 = fb_max(y, 0);
    int x1 = fb_min(x + w, (int)s_fb.width);
    int y1 = fb_min(y + h, (int)s_fb.height);
    int cols = x1 - x0;
    if (cols <= 0) return;

    if (s_fb.identity_layout) {
        /*
         * Fast path: back-buffer pixel == hardware pixel (0x00RRGGBB).
         * Use "rep movsd" to copy an entire scanline in one burst.
         * Each iteration moves one 32-bit dword (1 pixel), so 'count' = cols.
         * This replaces cols×4 individual byte-writes with a single hardware
         * string instruction, giving ~4× better throughput to MMIO.
         */
        for (int row = y0; row < y1; row++) {
            unsigned int       *dst = (unsigned int *)(s_fb.front
                + (unsigned int)row * s_fb.pitch)
                + (unsigned int)x0;
            const unsigned int *src = s_fb.back
                + (unsigned int)row * s_fb.width
                + (unsigned int)x0;
            int cnt = cols;
            __asm__ volatile (
                "rep movsl"
                : "+D" (dst), "+S" (src), "+c" (cnt)
                :
                : "memory"
            );
        }
    } else {
        /* Slow path: non-standard channel order – remap each pixel. */
        for (int row = y0; row < y1; row++) {
            unsigned char      *dst_row = s_fb.front
                + (unsigned int)row * s_fb.pitch
                + (unsigned int)x0 * (s_fb.bpp / 8u);
            const unsigned int *src_row = s_fb.back
                + (unsigned int)row * s_fb.width
                + (unsigned int)x0;
            for (int col = 0; col < cols; col++) {
                unsigned int hw = color_to_hw(src_row[col]);
                unsigned char *p = dst_row + col * 4;
                p[0] = (unsigned char)( hw        & 0xFF);
                p[1] = (unsigned char)((hw >>  8) & 0xFF);
                p[2] = (unsigned char)((hw >> 16) & 0xFF);
                p[3] = 0;
            }
        }
    }
}
