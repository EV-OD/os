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
#define PIC2_END    (PIC2_OFFSET + 7)

/* Command words */
#define PIC_EOI     0x20 /* End of Interrupt command */
// format of ICW4
/* 
ICW4
Bit 0: 1 = 8086/88 mode, 0 = MCS-80/85 mode
Bit 1: 1 = Auto EOI, 0 = Normal EOI
Bit 2: 1 = Buffered mode, 0 = Non-buffered mode
Bit 3: 1 = Special fully nested mode, 0 = Normal EOI
*/
#define ICW1_ICW4   0x01 /* ICW4 is used for initialization of PICs */
#define ICW1_INIT   0x10 /* Initialization - required! */
#define ICW4_8086   0x01 /* 8086/88 (MCS-80/85) mode */

void pic_remap(unsigned char offset1, unsigned char offset2);
void pic_acknowledge(unsigned int interrupt);
void pic_set_mask(unsigned char irq);
void pic_clear_mask(unsigned char irq);

#endif /* INCLUDE_PIC_H */
