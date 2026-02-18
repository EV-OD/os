# PIC Command/Port Cheatsheet

Quick reference for 8259 PIC ports and command words (one topic per sheet).

## Ports
- PIC1 command: 0x20
- PIC1 data   : 0x21 (also OCW1 mask)
- PIC2 command: 0xA0
- PIC2 data   : 0xA1 (also OCW1 mask)

## Initialization (ICWs)
- ICW1 (sent to command ports)
  - Bit 4 (0x10) INIT = 1 to start init
  - Bit 0 (0x01) ICW4 needed if set
- ICW2 (data port): Vector offset for this PIC (e.g., 0x20 for master, 0x28 for slave)
- ICW3 (data port): Cascade wiring
  - Master: bit set where a slave is attached (typically 0x04 for IRQ2)
  - Slave : ID of slave’s IRQ line (typically 0x02)
- ICW4 (data port)
  - Bit 0 (0x01): 8086/88 mode (set)
  - Bit 1 (0x02): Auto EOI (usually clear)

## Operation Commands
- OCW1 (data port): IRQ mask bits; 1 = masked/disabled
- OCW2 (command port): EOI and rotate
  - 0x20 = Non-specific EOI (used here)
- OCW3 (command port): Poll/IRR/ISR selection (not used here)

## Typical Remap Sequence (values)
1) Save masks (read data ports)
2) ICW1: 0x11 to both command ports (INIT + ICW4)
3) ICW2: 0x20 to PIC1 data, 0x28 to PIC2 data
4) ICW3: 0x04 to PIC1 data (slave on IRQ2), 0x02 to PIC2 data (ID = 2)
5) ICW4: 0x01 to both data ports (8086 mode)
6) Restore masks

## Acknowledge (EOI)
- Send 0x20 to PIC1 command
- If IRQ came from slave (vector >= 0x28), send 0x20 to PIC2 command first, then PIC1

## Quick meanings
- Vector offset sets base interrupt number for IRQ0..7 / IRQ8..15.
- Mask bit = 1 disables that IRQ line.
- INIT without remap risks clashing with CPU exceptions (0x00–0x1F).
