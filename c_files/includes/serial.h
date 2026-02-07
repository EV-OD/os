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


/*
Configuring the Modem

0x03 = 00000011 (RTS = 1 and DTS = 1).
Bit:     | 7 | 6 | 5  | 4  | 3   | 2   | 1   | 0   |
Content: | r | r | af | lb | ao2 | ao1 | rts | dtr |


We don’t need to enable interrupts, because we won’t handle any received data. Therefore we use the configuration value 0x03 = 00000011 (RTS = 1 and DTS = 1).


*/


#define SERIAL_MODEM_CONFIG 0x03



/*
SERIAL_FIFO_EMPTY:
    * Transmit FIFO queue is empty
    * Bit 5 of the Line Status Register (LSR) indicates whether the transmit FIFO queue is empty.
    * If this bit is set (1), it means that the FIFO queue is empty and ready to accept new data for transmission.
    * If this bit is clear (0), it means that there is still data in the FIFO queue waiting to be transmitted.
    * 
0x20 = 0010 0000 
*/
#define  SERIAL_FIFO_EMPTY 0x20

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
 *  @param  com The serial port to configure
 */
void serial_configure_line(unsigned short com);


/** serial_configure_fifo:
 *  Configures the FIFO buffer of the given serial port.
 *
 *  @param com  The serial port to configure
 */
void serial_configure_fifo(unsigned short com);


/** serial_configure_modem:
 *  Configures the modem of the given serial port.
 *
 *  @param com  The serial port to configure
 *
 */
void serial_configure_modem(unsigned short com);


/** serial_is_transmit_fifo_empty_com:
 *  Checks whether the transmit FIFO queue is empty or not for the given COM
 *  port.
 *
 *  @param  com The COM port
 *  @return 0 if the transmit FIFO queue is not empty
 *          1 if the transmit FIFO queue is empty
 */
int serial_is_transmit_fifo_empty_com(unsigned int com);


/** serial_is_transmit_fifo_empty:
 *  Checks whether the transmit FIFO queue is empty or not for SERIAL_COM1_BASE.
 *
 *  @return 0 if the transmit FIFO queue is not empty
 *          1 if the transmit FIFO queue is empty
 */
int serial_is_transmit_fifo_empty(void);


/** serial_init_com:
 *  Initializes the given serial port.
 *
 *  @param com  The serial port to initialize
 */
void serial_init_com(unsigned short com);


/** serial_init:
 *  Initializes SERIAL_COM1_BASE.
 */
void serial_init(void);


/** serial_begin_com:
 *  Initializes the given serial port with a specific baud rate.
 *
 *  @param com        The serial port to initialize
 *  @param baud_rate  The desired baud rate (e.g., 9600, 115200)
 */
void serial_begin_com(unsigned short com, unsigned int baud_rate);


/** serial_begin:
 *  Initializes SERIAL_COM1_BASE with a specific baud rate.
 *
 *  @param baud_rate  The desired baud rate (e.g., 9600, 115200)
 */
void serial_begin(unsigned int baud_rate);


/** serial_write_char_com:
 *  Writes a character to the given serial port.
 *
 *  @param com  The serial port to write to
 *  @param c    The character to write
 */
void serial_write_char_com(unsigned short com, char c);


/** serial_write_char:
 *  Writes a character to SERIAL_COM1_BASE.
 *
 *  @param c    The character to write
 */
void serial_write_char(char c);


/** serial_write_com:
 *  Writes a buffer of characters to the given serial port.
 *
 *  @param com  The serial port to write to
 *  @param buf  The buffer of characters
 *  @param len  The length of the buffer
 */
void serial_write_com(unsigned short com, char *buf, unsigned int len);


/** serial_write:
 *  Writes a buffer of characters to SERIAL_COM1_BASE.
 *
 *  @param buf  The buffer of characters
 *  @param len  The length of the buffer
 */
void serial_write(char *buf, unsigned int len);

#endif /* INCLUDE_SERIAL_H */