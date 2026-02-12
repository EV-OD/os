

#ifndef DESCRIPTOR_H
#define DESCRIPTOR_H

/* GDT segment descriptor (8 bytes) */
struct gdt_entry {
    unsigned short limit_low;    /* Lower 16 bits of the limit */
    unsigned short base_low;     /* Lower 16 bits of the base */
    unsigned char  base_middle;  /* Next 8 bits of the base */
    unsigned char  access;       /* Access flags (type, DPL, present) */
    unsigned char  granularity;  /* Granularity + upper 4 bits of limit */
    unsigned char  base_high;    /* Upper 8 bits of the base */
} __attribute__((packed));

/* GDTR pointer structure passed to lgdt */
struct gdt_ptr {
    unsigned short size;     /* Size of GDT in bytes minus 1 */
    unsigned int   address;  /* Linear address of the GDT */
} __attribute__((packed));

/* Number of GDT entries: null + kernel code + kernel data */
#define GDT_NUM_ENTRIES 3

void gdt_init(void);

#endif /* DESCRIPTOR_H */


