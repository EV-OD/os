#ifndef INCLUDE_PIC_H
#define INCLUDE_PIC_H

/* PIC I/O ports */
#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

/* Default remap offsets (vector numbers) */
#define PIC1_OFFSET 0x20
#define PIC2_OFFSET 0x28

/* Command words */
#define PIC_EOI     0x20
#define ICW1_ICW4   0x01
#define ICW1_INIT   0x10
#define ICW4_8086   0x01

void pic_remap(unsigned char offset1, unsigned char offset2);
void pic_acknowledge(unsigned int interrupt);
void pic_set_mask(unsigned char irq);
void pic_clear_mask(unsigned char irq);

#endif /* INCLUDE_PIC_H */
