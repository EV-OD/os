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
    gdt[index].base_low    = (base & 0xFFFF);
    gdt[index].base_middle = (base >> 16) & 0xFF;
    gdt[index].base_high   = (base >> 24) & 0xFF;

    gdt[index].limit_low   = (limit & 0xFFFF);
    gdt[index].granularity  = ((limit >> 16) & 0x0F) | (gran & 0xF0);

    gdt[index].access = access;
}

void gdt_init(void)
{
    /* GDTR: size = (num_entries * 8) - 1 */
    gp.size    = (sizeof(struct gdt_entry) * GDT_NUM_ENTRIES) - 1;
    gp.address = (unsigned int)&gdt;

    /* Entry 0: Null descriptor (all zeros) */
    gdt_set_entry(0, 0, 0, 0, 0);

    /*
     * Entry 1: Kernel Code Segment  (selector 0x08)
     *   Base  = 0x00000000
     *   Limit = 0xFFFFF (with 4 KiB granularity = 4 GB)
     *   Access = 0x9A:
     *     P=1, DPL=00, S=1, Type=1010 (Execute/Read)
     *   Granularity = 0xCF:
     *     G=1, D=1, L=0, AVL=0, Limit[19:16]=0xF
     */
    gdt_set_entry(1, 0x00000000, 0xFFFFF, 0x9A, 0xCF);

    /*
     * Entry 2: Kernel Data Segment  (selector 0x10)
     *   Base  = 0x00000000
     *   Limit = 0xFFFFF (with 4 KiB granularity = 4 GB)
     *   Access = 0x92:
     *     P=1, DPL=00, S=1, Type=0010 (Read/Write)
     *   Granularity = 0xCF:
     *     G=1, D=1, L=0, AVL=0, Limit[19:16]=0xF
     */
    gdt_set_entry(2, 0x00000000, 0xFFFFF, 0x92, 0xCF);

    /* Load the GDT and flush segment registers */
    gdt_load((unsigned int)&gp);
}
