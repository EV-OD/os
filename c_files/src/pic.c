#include "pic.h"
#include "stdio.h"

/* Remap PIC1 and PIC2 to the supplied vector offsets. */
void pic_remap(unsigned char offset1, unsigned char offset2)
{
    // Save current masks.
    // This is important to avoid losing any existing interrupt masks during remapping.
    // PIC1_DATA pin has the interrupt mask for the master PIC, which controls IRQs 0-7.
    // PIC2_DATA pin has the interrupt mask for the slave PIC, which controls IRQs
    unsigned char mask1 = inb(PIC1_DATA);
    unsigned char mask2 = inb(PIC2_DATA);


    // Start the initialization sequence in cascade mode. ICW1_INIT indicates that we are initializing the PICs, and ICW1_ICW4 indicates that ICW4 will be sent during initialization.
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);



    // Set the vector offsets for the master and slave PICs. This tells the CPU where to find the interrupt handlers for each IRQ line. The master PIC will use the offset specified by offset1, and the slave PIC will use the offset specified by offset2.
    outb(PIC1_DATA, offset1); /* ICW2: master offset */
    outb(PIC2_DATA, offset2); /* ICW2: slave offset */


    // Configure the cascading between the master and slave PICs. The master PIC needs to know that there is a slave PIC connected to IRQ2, and the slave PIC needs to know its cascade identity (which is 2 in this case). This is done using ICW3.
    outb(PIC1_DATA, 0x04);    /* ICW3: tell master about slave on IRQ2 */
    outb(PIC2_DATA, 0x02);    /* ICW3: tell slave its cascade identity */


    // Set the PICs to operate in 8086/88 mode. This is done using ICW4, and it tells the PICs to use the standard x86 interrupt handling mode.
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);


    // Restore saved masks. This is important to ensure that any previously set interrupt masks are preserved after remapping.
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
