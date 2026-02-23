#include "kernel_init.h"
#include "display.h"
#include "module.h"

void kmain(unsigned int eax, unsigned int ebx)
{
    kernel_init();
    display_boot_info();
    module_run(eax, ebx);
}
