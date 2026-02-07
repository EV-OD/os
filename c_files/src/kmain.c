#include "stdio.h"
#include "string.h"
#include "serial.h"


void kmain()
{

    serial_begin(9600);
    fb_clear();
    cursor_move_home();
    
    char buf[128];
    sprintf(buf, "OS loaded. Version: %d. Subsystem: %s. Code: %c", 1, "String", 'A');
    serial_write(buf);
    puts(buf);
}
