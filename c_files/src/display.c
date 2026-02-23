#include "display.h"
#include "stdio.h"
#include "string.h"
#include "serial.h"

#define KERNEL_VERSION "0.0.1"

void display_boot_info(void)
{
    char buf[128];

    fb_clear();
    cursor_move_home();

    sprintf(buf, "Kernel version: %s\n", KERNEL_VERSION);
    puts(buf);

    sprintf(buf, "OS loaded. Version: %s. Subsystem: %s. Code: %c\n",
            KERNEL_VERSION, "String", 'A');
    serial_write(buf);
    puts(buf);
}
