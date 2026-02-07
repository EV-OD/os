#include "stdio.h"
#include "serial.h"

void serial_configure_baud_rate(unsigned short com, unsigned short divisor)
{
    outb(SERIAL_LINE_COMMAND_PORT(com), SERIAL_LINE_ENABLE_DLAB);
    outb(SERIAL_DATA_PORT(com), (divisor >> 8) & 0x00FF);
    outb(SERIAL_DATA_PORT(com), divisor & 0x00FF);
}


void serial_configure_line(unsigned short com)
{
    outb(SERIAL_LINE_COMMAND_PORT(com), SERIAL_LINE_CONFIG_8N1);
}


void serial_configure_fifo(unsigned short com)
{
    outb(SERIAL_FIFO_COMMAND_PORT(com), SERIAL_FIFO_ENABLE_14BYTES);
}


void serial_configure_modem(unsigned short com)
{
    outb(SERIAL_MODEM_COMMAND_PORT(com), SERIAL_MODEM_CONFIG);
}


int serial_is_transmit_fifo_empty(unsigned int com)
{
    return inb(SERIAL_LINE_STATUS_PORT(com)) & SERIAL_FIFO_EMPTY;
}


void serial_init(unsigned short com)
{
    serial_configure_baud_rate(com, 3);
    serial_configure_line(com);
    serial_configure_fifo(com);
    serial_configure_modem(com);
}


void serial_write_char(unsigned short com, char c)
{
    while (serial_is_transmit_fifo_empty(com) == 0);
    outb(SERIAL_DATA_PORT(com), c);
}


void serial_write(unsigned short com, char *buf, unsigned int len)
{
    for (unsigned int i = 0; i < len; i++) {
        serial_write_char(com, buf[i]);
    }
}

