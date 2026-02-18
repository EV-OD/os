# Programmable Interrupt Controller (PIC)

This note explains how the PIC is configured and acknowledged in the kernel. The code lives in [c_files/src/pic.c](../../c_files/src/pic.c) with the public API in [c_files/includes/pic.h](../../c_files/includes/pic.h).

## Ports and Offsets

- Master (PIC1): command 0x20, data 0x21.
- Slave  (PIC2): command 0xA0, data 0xA1.
- Default remap vectors: master starts at 0x20, slave at 0x28. This avoids collisions with CPU exceptions (0x00-0x1F).

## Initialization

`pic_remap(offset1, offset2)` performs the standard 8259 reprogramming sequence:

1. Save existing interrupt masks from both PICs.
2. Send ICW1 (`ICW1_INIT | ICW1_ICW4`) to enter initialization.
3. Send ICW2 to set the new interrupt vector offsets (one for each PIC).
4. Send ICW3 to declare the cascade: master has a slave on IRQ2 (0x04), slave identity is 0x02.
5. Send ICW4 (`ICW4_8086`) to use 8086/88 mode.
6. Restore the saved masks so previously masked IRQs stay masked.

## Acknowledging Interrupts

`pic_acknowledge(interrupt)` sends an End Of Interrupt (EOI) command to the PIC that raised the interrupt. If the interrupt vector is within the slave range (>= `PIC2_OFFSET`), it acknowledges the slave first, then the master, as required by the hardware.

## Mask Control

- `pic_set_mask(irq)` sets the mask bit for a given IRQ line (disables delivery).
- `pic_clear_mask(irq)` clears the mask bit (enables delivery).

These helpers keep the rest of the kernel from hand-rolling port writes every time an IRQ needs to be toggled.

## Usage Notes

- Always remap before unmasking hardware IRQs to avoid conflicts with CPU exceptions.
- Remember to acknowledge every handled IRQ to allow subsequent interrupts from the same line.
