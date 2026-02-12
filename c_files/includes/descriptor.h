

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
 *   Index 0 : Null descriptor   (required by the CPU, must be all zeros)
 *   Index 1 : Kernel code segment  (selector = index * 8 = 0x08)
 *   Index 2 : Kernel data segment  (selector = index * 8 = 0x10)
 *
 * Selector value = index * 8  because each descriptor is 8 bytes and
 * the lower 3 bits of a selector encode TI (bit 2) and RPL (bits 1-0).
 */
#define GDT_NUM_ENTRIES 3

/* Segment selector constants — index * 8, with TI=0 (GDT) and RPL=00 (PL0) */
#define GDT_KERNEL_CODE_SELECTOR 0x08  /* Index 1: 1 * 8 = 0x08 */
#define GDT_KERNEL_DATA_SELECTOR 0x10  /* Index 2: 2 * 8 = 0x10 */

void gdt_init(void);

#endif /* DESCRIPTOR_H */


