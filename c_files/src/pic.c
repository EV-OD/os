#include "pic.h"
#include "stdio.h"

/* Remap PIC1 and PIC2 to the supplied vector offsets. */
void pic_remap(unsigned char offset1, unsigned char offset2)
{
    unsigned char mask1 = inb(PIC1_DATA);
    unsigned char mask2 = inb(PIC2_DATA);

    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);

    outb(PIC1_DATA, offset1); /* ICW2: master offset */
    outb(PIC2_DATA, offset2); /* ICW2: slave offset */

    outb(PIC1_DATA, 0x04);    /* ICW3: tell master about slave on IRQ2 */
    outb(PIC2_DATA, 0x02);    /* ICW3: tell slave its cascade identity */

    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

void pic_acknowledge(unsigned int interrupt)
{
    /* Only acknowledge interrupts in the remapped range. */
    if (interrupt < PIC1_OFFSET || interrupt >= PIC2_OFFSET + 8) {
        return;
    }

    /* If the interrupt came from the slave, acknowledge it first. */
    if (interrupt >= PIC2_OFFSET) {
        outb(PIC2_COMMAND, PIC_EOI);
    }

    outb(PIC1_COMMAND, PIC_EOI);
}

void pic_set_mask(unsigned char irq)
{
    unsigned short port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) {
        irq -= 8;
    }
    unsigned char mask = inb(port) | (1 << irq);
    outb(port, mask);
}

void pic_clear_mask(unsigned char irq)
{
    unsigned short port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) {
        irq -= 8;
    }
    unsigned char mask = inb(port) & ~(1 << irq);
    outb(port, mask);
}
