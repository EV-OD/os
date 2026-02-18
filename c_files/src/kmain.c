#include "stdio.h"
#include "string.h"
#include "serial.h"
#include "descriptor.h"
#include "idt.h"
#include "isr.h"
#include "pic.h"


void kmain()
{
    gdt_init();

    idt_init();
    isr_install();
    pic_remap(PIC1_OFFSET, PIC2_OFFSET);

    serial_begin(9600);
    fb_clear();
    cursor_move_home();
    
    char buf[128];
    sprintf(buf, "OS loaded. Version: %d. Subsystem: %s. Code: %c", 1, "String", 'A');
    serial_write(buf);
    puts(buf);
}
