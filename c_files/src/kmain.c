#include "stdio.h"
#include "string.h"
#include "serial.h"
#include "descriptor.h"
#include "idt.h"
#include "isr.h"
#include "pic.h"
#include "keyboard.h"
#include "interrupts.h"


static void demo_io(void)
{
    puts("Input/output demo.\n");

    puts("Enter a word: ");
    char word[64];
    scanf("%s", word);

    puts("\nEnter a line: ");
    char linebuf[96];
    readline(linebuf, sizeof(linebuf));

    puts("\nEnter a number: ");
    int number = 0;
    scanf("%d", &number);

    puts("\nPress a single key: ");
    int key = getchar();

    char line[128];
    sprintf(line, "\nYou typed word=%s, line=%s, number=%d, key=%c\n", word, linebuf, number, (char)key);
    puts(line);
}


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
    unsigned char* version = (unsigned char*)"0.0.1";
    char buf[128];
    sprintf(buf, "Kernel version: %s\n", version);
    puts(buf);

    interrupts_enable();

    sprintf(buf, "OS loaded. Version: %s. Subsystem: %s. Code: %c\n", version, "String", 'A');
    serial_write(buf);
    puts(buf);

    demo_io();
}
