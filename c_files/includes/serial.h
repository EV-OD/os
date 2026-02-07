#ifndef INCLUDE_SERIAL_H
#define INCLUDE_SERIAL_H

#define SERIAL_COM1_BASE                0x3F8      /* COM1 base port */

#define SERIAL_DATA_PORT(base)          (base)         // Used to send or receive the actual characters (like 'A', 'B', 'C'). If you write a byte here, it gets transmitted.
#define SERIAL_FIFO_COMMAND_PORT(base)  (base + 2)     // Controls the FIFO (First-In, First-Out) buffer. Since the CPU is much faster than the serial line, the hardware has a tiny "waiting room" (buffer) for characters. This port is used to enable or clear that waiting room.
#define SERIAL_LINE_COMMAND_PORT(base)  (base + 3)     // Configures the serial line settings, such as baud rate (speed), number of data bits, parity, and stop bits. This is crucial for ensuring that both ends of the communication understand each other.
#define SERIAL_MODEM_COMMAND_PORT(base) (base + 4)     // Used for "handshaking." It tells the device on the other end, "I am ready to receive data" (Ready To Transmit/Request To Send).
#define SERIAL_LINE_STATUS_PORT(base)   (base + 5)     // Provides status information about the serial line, such as whether data is available to read or if the transmitter is ready to send more data.



/* The I/O port commands */

/* SERIAL_LINE_ENABLE_DLAB:
    * Tells the serial port to expect first the highest 8 bits on the data port,
    * then the lowest 8 bits will follow
*/
#define SERIAL_LINE_ENABLE_DLAB         0x80



/* SERIAL_LINE_CONFIG_8N1:
    * 8 bits, no parity, one stop bit
    * Bit:         | 7 | 6 | 5 4 3 | 2 | 1 0 |
        * Content: | d | b | prty  | s | dl  |
        * Value:   | 0 | 0 | 0 0 0 | 0 | 1 1 | = 0x03
    *
*/

#define SERIAL_LINE_CONFIG_8N1         0x03

/*
Enables FIFO
Clear both receiver and transmission FIFO queues
Use 14 bytes as size of queue

0xC7 = 11000111 

Bit:     | 7 6 | 5  | 4 | 3   | 2   | 1   | 0 |
Content: | lvl | bs | r | dma | clt | clr | e |
*/
#define  SERIAL_FIFO_ENABLE_14BYTES      0xC7

/** serial_configure_baud_rate:
 *  Sets the speed of the data being sent. The default speed of a serial
 *  port is 115200 bits/s. The argument is a divisor of that number, hence
 *  the resulting speed becomes (115200 / divisor) bits/s.
 *
 *  @param com      The COM port to configure
 *  @param divisor  The divisor
 */
void serial_configure_baud_rate(unsigned short com, unsigned short divisor);


/** serial_configure_line:
 *  Configures the line of the given serial port. The port is set to have a
 *  data length of 8 bits, no parity bits, one stop bit and break control
 *  disabled.
 *
 *  @param com  The serial port to configure
 */
void serial_configure_line(unsigned short com);

#endif /* INCLUDE_SERIAL_H */