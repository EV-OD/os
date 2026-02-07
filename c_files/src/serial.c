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


int serial_is_transmit_fifo_empty_com(unsigned int com)
{
    return inb(SERIAL_LINE_STATUS_PORT(com)) & SERIAL_FIFO_EMPTY;
}


int serial_is_transmit_fifo_empty(void)
{
    return serial_is_transmit_fifo_empty_com(SERIAL_COM1_BASE);
}


void serial_init_com(unsigned short com)
{
    serial_begin_com(com, 38400);
}


void serial_init(void)
{
    serial_init_com(SERIAL_COM1_BASE);
}


void serial_begin_com(unsigned short com, unsigned int baud_rate)
{
    unsigned short divisor = 115200 / baud_rate;
    serial_configure_baud_rate(com, divisor);
    serial_configure_line(com);
    serial_configure_fifo(com);
    serial_configure_modem(com);
}


void serial_begin(unsigned int baud_rate)
{
    serial_begin_com(SERIAL_COM1_BASE, baud_rate);
}


void serial_write_char_com(unsigned short com, char c)
{
    while (serial_is_transmit_fifo_empty_com(com) == 0);
    outb(SERIAL_DATA_PORT(com), c);
}


void serial_write_char(char c)
{
    serial_write_char_com(SERIAL_COM1_BASE, c);
}


void serial_write_com(unsigned short com, char *buf, unsigned int len)
{
    for (unsigned int i = 0; i < len; i++) {
        serial_write_char_com(com, buf[i]);
    }
}


void serial_write(char *buf, unsigned int len)
{
    serial_write_com(SERIAL_COM1_BASE, buf, len);
}

