# Serial Port Configuration

## Line Control Register

The data transmission format is configured via the line control port by sending a byte with the following layout:

| Bit | 7 | 6 | 5–3 | 2 | 1–0 |
|-----|---|---|-----|---|-----|
| Content | d | b | prty | s | dl |

### Field Descriptions

| Name | Description |
|------|-------------|
| d | Enables (1) or disables (0) DLAB |
| b | Break control enabled (1) or disabled (0) |
| prty | Number of parity bits to use |
| s | Number of stop bits (0 = 1, 1 = 1.5 or 2) |
| dl | Data length |

### Detailed Bit Mapping

| Bit | Name | Purpose |
|-----|------|---------|
| 7 | DLAB | Divisor Latch Access Bit (0 = Data, 1 = Divisor) |
| 6 | Break | Sets break condition |
| 5 | Stick | Stick parity |
| 4 | EPS | Even Parity Select |
| 3 | PEN | Parity Enable |
| 2 | STB | Number of Stop Bits |
| 1–0 | WLS | Word Length Select (e.g., 8 bits, 7 bits) |

## FIFO Buffer Configuration

Data transmitted via serial port is placed in FIFO buffers. The configuration byte layout:

| Bit | 7–6 | 5 | 4 | 3 | 2 | 1 | 0 |
|-----|-----|---|---|---|---|---|---|
| Content | lvl | bs | r | dma | clt | clr | e |

### Field Descriptions

| Name | Description |
|------|-------------|
| lvl | Bytes stored in FIFO buffers |
| bs | Buffer size: 16 or 64 bytes |
| r | Reserved |
| dma | Data access method |
| clt | Clear transmission FIFO |
| clr | Clear receiver FIFO |
| e | Enable FIFO buffer |

**Configuration value: `0xC7` (11000111)**
- Enables FIFO
- Clears both receiver and transmission queues
- Sets queue size to 14 bytes

## Modem Control Register

Used for hardware flow control via Ready To Transmit (RTS) and Data Terminal Ready (DTR) pins.

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|-----|---|---|---|---|---|---|---|---|
| Content | r | r | af | lb | ao2 | ao1 | rts | dtr |

### Field Descriptions

| Name | Description |
|------|-------------|
| r | Reserved |
| af | Autoflow control enabled |
| lb | Loopback mode (debugging) |
| ao2 | Auxiliary output 2 (receive interrupts) |
| ao1 | Auxiliary output 1 |
| rts | Ready To Transmit |
| dtr | Data Terminal Ready |

**Configuration value: `0x03` (00000011)**
- Sets RTS = 1 and DTR = 1 (ready to transmit)
- Interrupts disabled