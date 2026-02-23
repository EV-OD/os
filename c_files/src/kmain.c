#include "stdio.h"
#include "string.h"
#include "serial.h"
#include "descriptor.h"
#include "idt.h"
#include "isr.h"
#include "pic.h"
#include "keyboard.h"
#include "interrupts.h"
#include "multiboot.h"


typedef void (*call_module_t)(void);

void kmain(unsigned int eax, unsigned int ebx)
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

    /* Verify GRUB magic number */
    if (eax != MULTIBOOT_BOOTLOADER_MAGIC) {
        puts("ERROR: Not booted by a Multiboot-compliant bootloader!\n");
        return;
    }

    multiboot_info_t *mbinfo = (multiboot_info_t *) ebx;

    /* Check that modules info is available */
    if (!(mbinfo->flags & MULTIBOOT_INFO_MODS)) {
        puts("ERROR: No modules info available from bootloader!\n");
        return;
    }

    /* Check that exactly 1 module was loaded */
    if (mbinfo->mods_count != 1) {
        sprintf(buf, "ERROR: Expected 1 module, got %d\n", mbinfo->mods_count);
        puts(buf);
        return;
    }

    /* Get the address of the first (and only) module */
    multiboot_module_t *module = (multiboot_module_t *) mbinfo->mods_addr;
    unsigned int address_of_module = module->mod_start;

    sprintf(buf, "Module loaded at address: 0x%x\n", address_of_module);
    puts(buf);
    serial_write(buf);

    /* Jump to the loaded module */
    call_module_t start_program = (call_module_t) address_of_module;
    start_program();
    /* we'll never get here, unless the module code returns */
}
