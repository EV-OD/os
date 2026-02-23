#include "kernel_init.h"
#include "descriptor.h"
#include "idt.h"
#include "isr.h"
#include "pic.h"
#include "keyboard.h"
#include "serial.h"
#include "interrupts.h"

void kernel_init(void)
{
    gdt_init();
    idt_init();
    isr_install();
    pic_remap(PIC1_OFFSET, PIC2_OFFSET);
    keyboard_init();
    serial_begin(9600);
    interrupts_enable();
}
