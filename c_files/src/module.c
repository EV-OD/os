#include "module.h"
#include "multiboot.h"
#include "paging.h"
#include "stdio.h"
#include "string.h"

typedef void (*call_module_t)(void);

int module_run(unsigned int eax, unsigned int ebx)
{
    char buf[128];

    /* Verify GRUB magic number */
    if (eax != MULTIBOOT_BOOTLOADER_MAGIC) {
        puts("ERROR: Not booted by a Multiboot-compliant bootloader!\n");
        return -1;
    }

    /*
     * ebx was already adjusted in loader.s to a virtual address
     * (physical pointer + KERNEL_VIRTUAL_BASE).  Dereference directly.
     */
    multiboot_info_t *mbinfo = (multiboot_info_t *) ebx;

    /* Check that modules info is available */
    if (!(mbinfo->flags & MULTIBOOT_INFO_MODS)) {
        puts("ERROR: No modules info available from bootloader!\n");
        return -2;
    }

    /* Check that exactly 1 module was loaded */
    if (mbinfo->mods_count != 1) {
        sprintf(buf, "ERROR: Expected 1 module, got %d\n", mbinfo->mods_count);
        puts(buf);
        return -3;
    }

    /*
     * mods_addr is a physical address inside the multiboot info area.
     * Add KERNEL_VIRTUAL_BASE to access it through the higher-half mapping.
     * Similarly, mod_start is a physical load address for the module.
     */
    multiboot_module_t *module = (multiboot_module_t *)(mbinfo->mods_addr + KERNEL_VIRTUAL_BASE);
    unsigned int address_of_module = module->mod_start + KERNEL_VIRTUAL_BASE;

    sprintf(buf, "Module loaded at address: 0x%x\n", address_of_module);
    puts(buf);

    /* Jump to the loaded module */
    call_module_t start_program = (call_module_t) address_of_module;
    start_program();

    /* Only reached if the module returns */
    return 0;
}
