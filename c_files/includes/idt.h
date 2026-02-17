#ifndef INCLUDE_IDT_H
#define INCLUDE_IDT_H

#include "descriptor.h"

#define IDT_NUM_ENTRIES 256

/* Attribute bits for IDT gates (type_attr field). */
#define IDT_FLAG_PRESENT       0x80
#define IDT_FLAG_RING0         0x00
#define IDT_FLAG_RING3         0x60
#define IDT_FLAG_INTERRUPT_32  0x0E
#define IDT_FLAG_TRAP_32       0x0F

struct idt_entry {
    unsigned short offset_low;   /* Handler address bits 0..15 */
    unsigned short selector;     /* Code segment selector in GDT */
    unsigned char  zero;         /* Always zero */
    unsigned char  type_attr;    /* Type and attributes */
    unsigned short offset_high;  /* Handler address bits 16..31 */
} __attribute__((packed));

struct idt_ptr {
    unsigned short limit; /* Size of table in bytes minus one */
    unsigned int   base;  /* Linear address of first entry */
} __attribute__((packed));

/* Register/stack snapshots delivered to C interrupt handlers. */
struct cpu_state {
    unsigned int eax;
    unsigned int ebx;
    unsigned int ecx;
    unsigned int edx;
    unsigned int esi;
    unsigned int edi;
    unsigned int ebp;
    unsigned int esp;
} __attribute__((packed));

struct stack_state {
    unsigned int error_code;
    unsigned int eip;
    unsigned int cs;
    unsigned int eflags;
} __attribute__((packed));

void idt_init(void);
void idt_set_gate(int num, unsigned int base, unsigned short selector, unsigned char flags);

#endif /* INCLUDE_IDT_H */
