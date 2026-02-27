/* =========================================================================
 * kernel_init.c – Kernel boot-time initialisation sequence
 *
 * Sets up all core subsystems in dependency order:
 *   GDT → IDT → ISR stubs → PIC remap → keyboard → serial → paging → sti
 *
 * The order matters: see docs/kernel/boot_sequence.md for rationale.
 * ========================================================================= */

#include "kernel_init.h"
#include "descriptor.h"
#include "idt.h"
#include "isr.h"
#include "pic.h"
#include "keyboard.h"
#include "serial.h"
#include "interrupts.h"
#include "paging.h"

void kernel_init(void)
{
    gdt_init();
    idt_init();
    isr_install();
    pic_remap(PIC1_OFFSET, PIC2_OFFSET);
    keyboard_init();
    serial_begin(9600);
    /* Paging was enabled in loader.s before kmain; log and verify here
     * after the serial port is ready so the output is visible. */
    paging_init();
    interrupts_enable();
}
