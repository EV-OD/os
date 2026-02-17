#include "idt.h"
#include "string.h"

static struct idt_entry idt[IDT_NUM_ENTRIES];
static struct idt_ptr   idt_descriptor;

/* Implemented in asm/idt.s */
extern void idt_load(unsigned int idt_ptr_addr);

void idt_set_gate(int num, unsigned int base, unsigned short selector, unsigned char flags)
{
    idt[num].offset_low  = base & 0xFFFF;
    idt[num].selector    = selector;
    idt[num].zero        = 0;
    idt[num].type_attr   = flags;
    idt[num].offset_high = (base >> 16) & 0xFFFF;
}

void idt_init(void)
{
    memset(idt, 0, sizeof(idt));

    idt_descriptor.limit = (sizeof(struct idt_entry) * IDT_NUM_ENTRIES) - 1;
    idt_descriptor.base  = (unsigned int)&idt;

    idt_load((unsigned int)&idt_descriptor);
}
