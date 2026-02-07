The way that data should be sent must be configured. This is also done via the line command port by sending a byte. The layout of the 8 bits looks like the following:

Bit:     | 7 | 6 | 5 4 3 | 2 | 1 0 |
Content: | d | b | prty  | s | dl  |
A description for each name can be found in the table below (and in [31]):

Name	Description
d	Enables (d = 1) or disables (d = 0) DLAB
b	If break control is enabled (b = 1) or disabled (b = 0)
prty	The number of parity bits to use
s	The number of stop bits to use (s = 0 equals 1, s = 1 equals 1.5 or 2)
dl	Describes the length of the data



Bit	Name	Purpose
7	DLAB	Divisor Latch Access Bit (0 = Data, 1 = Divisor)
6	Break	Sets "break" condition
5	Stick	Stick parity
4	EPS	Even Parity Select
3	PEN	Parity Enable
2	STB	Number of Stop Bits
1-0	WLS	Word Length Select (e.g., 8 bits, 7 bits)



Configuring the Buffers
When data is transmitted via the serial port it is placed in buffers, both when receiving and sending data. This way, if you send data to the serial port faster than it can send it over the wire, it will be buffered. However, if you send too much data too fast the buffer will be full and data will be lost. In other words, the buffers are FIFO queues. The FIFO queue configuration byte looks like the following figure:

Bit:     | 7 6 | 5  | 4 | 3   | 2   | 1   | 0 |
Content: | lvl | bs | r | dma | clt | clr | e |
A description for each name can be found in the table below:

Name	Description
lvl	How many bytes should be stored in the FIFO buffers
bs	If the buffers should be 16 or 64 bytes large
r	Reserved for future use
dma	How the serial port data should be accessed
clt	Clear the transmission FIFO buffer
clr	Clear the receiver FIFO buffer
e	If the FIFO buffer should be enabled or not
We use the value 0xC7 = 11000111 that:

Enables FIFO
Clear both receiver and transmission FIFO queues
Use 14 bytes as size of queue





Configuring the Modem
The modem control register is used for very simple hardware flow control via the Ready To Transmit (RTS) and Data Terminal Ready (DTR) pins. When configuring the serial port we want RTS and DTR to be 1, which means that we are ready to send data.

The modem configuration byte is shown in the following figure:

Bit:     | 7 | 6 | 5  | 4  | 3   | 2   | 1   | 0   |
Content: | r | r | af | lb | ao2 | ao1 | rts | dtr |
A description for each name can be found in the table below:

Name	Description
r	Reserved
af	Autoflow control enabled
lb	Loopback mode (used for debugging serial ports)
ao2	Auxiliary output 2, used for receiving interrupts
ao1	Auxiliary output 1
rts	Ready To Transmit
dtr	Data Terminal Ready
We don’t need to enable interrupts, because we won’t handle any received data. Therefore we use the configuration value 0x03 = 00000011 (RTS = 1 and DTS = 1).