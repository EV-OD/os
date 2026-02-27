

#ifndef DESCRIPTOR_H
#define DESCRIPTOR_H

/*
 * GDT segment descriptor — 8 bytes, laid out exactly as the CPU expects.
 *
 * Full 8-byte descriptor layout (Intel Manual Vol. 3A, Figure 3-8):
 *
 *  Byte 7    Byte 6    Byte 5    Byte 4    Byte 3    Byte 2    Byte 1    Byte 0
 * +--------+---------+--------+---------+---------+---------+---------+--------+
 * |base    |G|D|L|AVL|seg lim |P|DPL|S|  |  base   |       base      | segment|
 * |31..24  | | | |   |19..16  | |   | |Ty|  23..16 |       15..0     | lim    |
 * |        | | | |   |        | |   | |pe|         |                 | 15..0  |
 * +--------+---------+--------+---------+---------+---------+---------+--------+
 *
 * The struct fields map to this layout in little-endian order.
 */
struct gdt_entry {
    unsigned short limit_low;    /* Bits  0-15 of the segment limit          */
    unsigned short base_low;     /* Bits  0-15 of the segment base address   */
    unsigned char  base_middle;  /* Bits 16-23 of the segment base address   */
    unsigned char  access;       /* Access byte (P, DPL, S, Type fields)     */
    unsigned char  granularity;  /* Flags (G, D/B, L, AVL) + limit bits 16-19*/
    unsigned char  base_high;    /* Bits 24-31 of the segment base address   */
} __attribute__((packed));

/*
 * GDTR pointer structure — passed to the lgdt instruction.
 * Must be packed because the CPU reads exactly 6 bytes:
 *   2-byte size (limit) + 4-byte linear address.
 * 'size' is always (total_bytes_of_GDT - 1), i.e. (num_entries * 8) - 1.
 */
struct gdt_ptr {
    unsigned short size;     /* sizeof(gdt_entries) - 1                      */
    unsigned int   address;  /* Linear (physical) address of gdt_entry[0]    */
} __attribute__((packed));

/*
 * GDT_NUM_ENTRIES — total number of 8-byte descriptors in our GDT.
 *   Index 0 : Null descriptor       (required by the CPU, must be all zeros)
 *   Index 1 : Kernel code segment   (selector = 0x08)  DPL=0  RX
 *   Index 2 : Kernel data segment   (selector = 0x10)  DPL=0  RW
 *   Index 3 : User   code segment   (selector = 0x18)  DPL=3  RX
 *   Index 4 : User   data segment   (selector = 0x20)  DPL=3  RW
 *   Index 5 : Task State Segment    (selector = 0x28)  type=TSS
 *
 * Selector value = index * 8  because each descriptor is 8 bytes and
 * the lower 3 bits of a selector encode TI (bit 2) and RPL (bits 1-0).
 * For user selectors the RPL=3 bits are ORed in at call sites (e.g. 0x18|3).
 */
#define GDT_NUM_ENTRIES 6

/* Kernel segment selectors — RPL=00 (ring 0) */
#define GDT_KERNEL_CODE_SELECTOR 0x08  /* Index 1: 1 * 8 = 0x08 */
#define GDT_KERNEL_DATA_SELECTOR 0x10  /* Index 2: 2 * 8 = 0x10 */

/* User segment selectors — base value (RPL bits not set; OR with 0x3 for ring-3 use) */
#define GDT_USER_CODE_SELECTOR   0x18  /* Index 3: 3 * 8 = 0x18 */
#define GDT_USER_DATA_SELECTOR   0x20  /* Index 4: 4 * 8 = 0x20 */

/* TSS selector */
#define GDT_TSS_SELECTOR         0x28  /* Index 5: 5 * 8 = 0x28 */

void gdt_init(void);

/**
 * gdt_set_tss_entry – write a 32-bit TSS descriptor into the GDT.
 *
 * Called by tss_init() after gdt_init() has created the GDT.  The TSS
 * descriptor has a slightly different format from regular segment descriptors
 * (type = 0x89: present, DPL=0, available 32-bit TSS).
 *
 * @param index   GDT index (must match TSS_GDT_INDEX in tss.h = 5).
 * @param base    Linear address of the TSS structure.
 * @param limit   sizeof(tss_entry_t) - 1.
 */
void gdt_set_tss_entry(int index, unsigned int base, unsigned int limit);

#endif /* DESCRIPTOR_H */


