#include "descriptor.h"

/* The actual GDT entries */
static struct gdt_entry gdt[GDT_NUM_ENTRIES];
static struct gdt_ptr   gp;

/* Defined in asm/gdt.s */
extern void gdt_load(unsigned int gdt_ptr_addr);

/*
 * Set up a single GDT entry.
 *
 * access byte layout:
 *   Bit 7    : Present (P)       - must be 1 for valid segments
 *   Bits 6-5 : DPL               - privilege level (0 = kernel)
 *   Bit 4    : Descriptor type   - 1 for code/data segments
 *   Bits 3-0 : Type field        - segment type (execute/read, read/write, etc.)
 *
 * granularity byte layout:
 *   Bit 7    : Granularity (G)   - 0 = byte, 1 = 4 KiB pages
 *   Bit 6    : Size (D/B)        - 0 = 16-bit, 1 = 32-bit
 *   Bit 5    : Long mode (L)     - 0 for 32-bit
 *   Bit 4    : Available (AVL)   - 0
 *   Bits 3-0 : Limit bits 19:16
 */
static void gdt_set_entry(int index, unsigned int base, unsigned int limit,
                           unsigned char access, unsigned char gran)
{
    /*
     * The 32-bit base address is split across three fields in the descriptor:
     *   base_low    = bits  0-15   (mask 0xFFFF)
     *   base_middle = bits 16-23   (shift right 16, mask 0xFF)
     *   base_high   = bits 24-31   (shift right 24, mask 0xFF)
     */
    gdt[index].base_low    = (base & 0xFFFF);         /* bits  0-15 */
    gdt[index].base_middle = (base >> 16) & 0xFF;     /* bits 16-23 */
    gdt[index].base_high   = (base >> 24) & 0xFF;     /* bits 24-31 */

    /*
     * The 20-bit segment limit is split across two locations:
     *   limit_low           = bits  0-15  (mask 0xFFFF)
     *   granularity[3:0]    = bits 16-19  (shift right 16, mask 0x0F)
     *
     * The upper nibble of 'granularity' (bits 7-4) holds the flags:
     *   G, D/B, L, AVL — passed in via 'gran' (mask 0xF0).
     * We OR them together so flags go in [7:4] and limit goes in [3:0].
     */
    gdt[index].limit_low   = (limit & 0xFFFF);                          /* bits  0-15 */
    gdt[index].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);   /* bits 16-19 | flags */

    gdt[index].access = access;
}

void gdt_init(void)
{
    /*
     * GDTR 'size' field = total bytes of GDT minus 1.
     * sizeof(gdt_entry) is always 8 (the CPU-defined descriptor size).
     * With 3 entries: (8 * 3) - 1 = 23 = 0x17.
     * The "-1" is required by the CPU — lgdt expects the last valid byte offset.
     */
    gp.size    = (sizeof(struct gdt_entry) * GDT_NUM_ENTRIES) - 1;
    gp.address = (unsigned int)&gdt;

    /*
     * Entry 0 (index 0) — Null descriptor.
     * The first GDT entry MUST be all zeros. The CPU requires it;
     * any segment selector that points here (selector = 0x00) will
     * cause a General Protection Fault if used to access memory.
     */
    gdt_set_entry(0, 0, 0, 0, 0);

    /*
     * Entry 1 (index 1) — Kernel Code Segment
     *   Selector = index * 8 = 0x08  (each descriptor is 8 bytes)
     *
     *   Base  = 0x00000000 — segment starts at the bottom of memory
     *   Limit = 0xFFFFF    — with G=1 (4 KiB pages): 0xFFFFF * 4096 = 4 GB
     *
     *   Access byte = 0x9A  (binary: 1001 1010)
     *     Bit 7   P    = 1    Segment is present in memory
     *     Bit 6-5 DPL  = 00   Ring 0 (kernel privilege)
     *     Bit 4   S    = 1    Code/data descriptor (not system)
     *     Bit 3   E    = 1    Executable (code segment)
     *     Bit 2   DC   = 0    Non-conforming (only ring 0 can execute)
     *     Bit 1   RW   = 1    Readable (can read code through cs)
     *     Bit 0   A    = 0    Accessed bit, CPU sets this
     *
     *   Granularity byte = 0xCF  (binary: 1100 1111)
     *     Bit 7   G    = 1    Limit is in 4 KiB pages (not bytes)
     *     Bit 6   D/B  = 1    32-bit protected mode segment
     *     Bit 5   L    = 0    Not a 64-bit code segment
     *     Bit 4   AVL  = 0    Available for OS use (unused)
     *     Bit 3-0 Lim  = 0xF  Upper 4 bits of 20-bit limit (0xFFFFF)
     */
    gdt_set_entry(1, 0x00000000, 0xFFFFF, 0x9A, 0xCF);

    /*
     * Entry 2 (index 2) — Kernel Data Segment
     *   Selector = index * 8 = 0x10  (each descriptor is 8 bytes)
     *
     *   Base  = 0x00000000 — segment starts at the bottom of memory
     *   Limit = 0xFFFFF    — with G=1 (4 KiB pages): 0xFFFFF * 4096 = 4 GB
     *
     *   Access byte = 0x92  (binary: 1001 0010)
     *     Bit 7   P    = 1    Segment is present in memory
     *     Bit 6-5 DPL  = 00   Ring 0 (kernel privilege)
     *     Bit 4   S    = 1    Code/data descriptor (not system)
     *     Bit 3   E    = 0    Not executable (data segment)
     *     Bit 2   DC   = 0    Grows up (expand-up data segment)
     *     Bit 1   RW   = 1    Writable (can write through ds/ss/es)
     *     Bit 0   A    = 0    Accessed bit, CPU sets this
     *
     *   Granularity byte = 0xCF  (same as code segment)
     *     G=1, D=1, L=0, AVL=0, Limit[19:16]=0xF
     */
    gdt_set_entry(2, 0x00000000, 0xFFFFF, 0x92, 0xCF);

    /*
     * Entry 3 (index 3) — User Code Segment
     *   Selector = index * 8 = 0x18  (RPL=3 added by callers: 0x18 | 0x3 = 0x1B)
     *
     *   Access byte = 0xFA  (binary: 1111 1010)
     *     Bit 7   P    = 1    Segment is present
     *     Bit 6-5 DPL  = 11   Ring 3 (user privilege)
     *     Bit 4   S    = 1    Code/data descriptor
     *     Bit 3   E    = 1    Executable (code segment)
     *     Bit 2   DC   = 0    Non-conforming
     *     Bit 1   RW   = 1    Readable
     *     Bit 0   A    = 0    Accessed bit
     *
     *   Granularity = 0xCF  (same as kernel code segment)
     */
    gdt_set_entry(3, 0x00000000, 0xFFFFF, 0xFA, 0xCF);

    /*
     * Entry 4 (index 4) — User Data Segment
     *   Selector = index * 8 = 0x20  (RPL=3 added by callers: 0x20 | 0x3 = 0x23)
     *
     *   Access byte = 0xF2  (binary: 1111 0010)
     *     Bit 7   P    = 1    Segment is present
     *     Bit 6-5 DPL  = 11   Ring 3 (user privilege)
     *     Bit 4   S    = 1    Code/data descriptor
     *     Bit 3   E    = 0    Not executable (data segment)
     *     Bit 2   DC   = 0    Grows up
     *     Bit 1   RW   = 1    Writable
     *     Bit 0   A    = 0    Accessed bit
     *
     *   Granularity = 0xCF  (same as kernel data segment)
     */
    gdt_set_entry(4, 0x00000000, 0xFFFFF, 0xF2, 0xCF);

    /*
     * Entry 5 (index 5) — TSS descriptor placeholder.
     * The actual TSS descriptor is written by tss_init() via gdt_set_tss_entry()
     * after the TSS structure address is known.  Leave it null for now.
     */
    gdt_set_entry(5, 0, 0, 0, 0);

    /* Load the GDT and flush segment registers */
    gdt_load((unsigned int)&gp);
}

/*
 * gdt_set_tss_entry – write a 32-bit available TSS descriptor into the GDT.
 *
 * Called by tss_init() once the address of the TSS structure is known.
 * The TSS descriptor has the same 8-byte layout as regular segments but:
 *   • S = 0 (bit 4 of access byte) – it is a "system descriptor"
 *   • Type = 1001 (bits 3-0 of access byte) – available 32-bit TSS
 *   • DPL = 0  – only kernel code can load TR (ltr) or call this gate
 *
 *   Access byte = 0x89  (1000 1001)
 *   Granularity = 0x00  (byte-level; TSS size fits in a few hundred bytes)
 *
 * @param index  GDT slot to write (must be TSS_GDT_INDEX = 5).
 * @param base   Linear address of the tss_entry_t struct.
 * @param limit  sizeof(tss_entry_t) - 1.
 */
void gdt_set_tss_entry(int index, unsigned int base, unsigned int limit)
{
    /*
     * 0x89 = 1000 1001
     *   P=1  (Present)
     *   DPL=00 (ring 0 only)
     *   S=0  (system descriptor, not code/data)
     *   Type=1001 (available 32-bit TSS)
     *
     * 0x00 granularity = byte-level, no 32-bit flag needed for TSS.
     */
    gdt_set_entry(index, base, limit, 0x89, 0x00);
}
