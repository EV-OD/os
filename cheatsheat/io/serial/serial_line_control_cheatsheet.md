# Serial Line Control Cheatsheet (COM ports)

## Ports (COM1 base 0x3F8)
- Data: base + 0 (0x3F8)
- Interrupt enable: base + 1
- FIFO control: base + 2
- Line control: base + 3
- Modem control: base + 4
- Line status: base + 5

## Line Control Register (LCR, base+3)
Bits: d b prty s dl
- Bit 7 (d) DLAB: 1 = divisor latch access, 0 = data regs
- Bit 6 (b) Break control
- Bits 5–3 (prty) parity select
- Bit 2 (s) stop bits (0=1 stop, 1=1.5/2 stop)
- Bits 1–0 (dl) word length (3=8N1 common)

Common value: **0x03** = 8N1, DLAB=0, no parity, 1 stop.

## Divisor Latch (DLAB=1, base+0/1)
- Baud divisor = 115200 / baud
- Write high byte to base+1, low byte to base+0

## FIFO Control (FCR, base+2)
Bits: lvl bs r dma clt clr e
- Bit 7–6 (lvl): trigger level
- Bit 5 (bs): FIFO size select
- Bit 4 (r): reserved
- Bit 3 (dma): DMA mode
- Bit 2 (clt): clear TX FIFO
- Bit 1 (clr): clear RX FIFO
- Bit 0 (e): enable FIFO

Common value: **0xC7** = enable FIFO, clear RX/TX, 14-byte trigger.

## Modem Control (MCR, base+4)
Bits: r r af lb ao2 ao1 rts dtr
- Bit 7–6 (r): reserved
- Bit 5 (af): autoflow
- Bit 4 (lb): loopback (debug)
- Bit 3 (ao2): aux output 2 (used for interrupts)
- Bit 2 (ao1): aux output 1
- Bit 1 (rts): Ready To Send
- Bit 0 (dtr): Data Terminal Ready

Common value: **0x03** = RTS=1, DTR=1, interrupts off, no loopback.

## Line Status (LSR, base+5)
- Bit 5 (0x20): Transmitter holding register empty
- Bit 0 (0x01): Data ready (RX has data)

## Quick recipes
1) Set baud: DLAB=1; write divisor high/low; DLAB=0.
2) Set 8N1: write 0x03 to LCR (DLAB cleared).
3) Enable FIFO: write 0xC7 to FCR.
4) Set RTS/DTR: write 0x03 to MCR.
