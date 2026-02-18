#include "stdio.h"
#include "string.h"
#include "serial.h"
#include "descriptor.h"
#include "idt.h"
#include "isr.h"
#include "pic.h"
#include "keyboard.h"
#include "interrupts.h"


void kmain()
{
    gdt_init();

    idt_init();
    isr_install();
    pic_remap(PIC1_OFFSET, PIC2_OFFSET);
    keyboard_init();

    serial_begin(9600);
    fb_clear();
    cursor_move_home();
    puts("Type on the keyboard to echo characters.\n");

    interrupts_enable();
    
    char buf[128];
    sprintf(buf, "OS loaded. Version: %d. Subsystem: %s. Code: %c", 1, "String", 'A');
    serial_write(buf);
    puts(buf);
}
