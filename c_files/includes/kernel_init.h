#ifndef KERNEL_INIT_H
#define KERNEL_INIT_H

/**
 * Initialize all core kernel subsystems:
 *   GDT, IDT, ISR, PIC, keyboard, serial port.
 * After this call, hardware interrupts are enabled.
 */
void kernel_init(void);

#endif /* KERNEL_INIT_H */
