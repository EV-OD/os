#include "kernel_init.h"
#include "display.h"
#include "stdio.h"
#include "module.h"

void kmain(unsigned int eax, unsigned int ebx)
{
    kernel_init();
    fb_clear();
    cursor_move_home();
    display_boot_info();
    module_run(eax, ebx);
}
