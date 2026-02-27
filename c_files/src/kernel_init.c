/* =========================================================================
 * kernel_init.c – Kernel boot-time initialisation sequence
 *
 * Sets up all core subsystems in dependency order:
 *   GDT → IDT → ISR stubs → PIC remap → keyboard → serial
 *   → paging verify → PFA (physical frame allocator) → kheap → sti
 *
 * The order matters – see docs/kernel/boot_sequence.md for rationale.
 *
 * Memory subsystem (new in this version)
 * ----------------------------------------
 * pfa_init() must run after serial is up (so log output is visible) and
 * after paging_init() has confirmed the page directory is sane.  It reads
 * the Multiboot memory map and reserves the kernel image frames using the
 * symbols exported by the linker script.
 *
 * kheap_init() must follow pfa_init() because in a later phase it will
 * use the PFA to extend the heap beyond the initial static reservation.
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
#include "pfa.h"
#include "kheap.h"
#include "log.h"

/*
 * Linker-script boundary symbols for the kernel image.
 * Taking the *address* of these symbols gives their numeric value
 * (physical or virtual start/end of the kernel binary in memory).
 * They are not real variables – no memory is accessed at runtime.
 */
extern unsigned int kernel_physical_start;
extern unsigned int kernel_physical_end;

void kernel_init(unsigned int mb_magic, multiboot_info_t *mb)
{
    gdt_init();
    idt_init();
    isr_install();
    pic_remap(PIC1_OFFSET, PIC2_OFFSET);
    keyboard_init();
    serial_begin(9600);

    /*
     * Verify paging state – must come after serial so the log lines
     * are visible on the COM1 monitor.
     */
    paging_init();

    /*
     * Initialise the physical frame allocator.
     * Pass the Multiboot info pointer so pfa_init() can walk the
     * memory map, plus the physical addresses of the kernel image
     * boundaries so it can mark those frames as reserved.
     */
    if (mb_magic == MULTIBOOT_BOOTLOADER_MAGIC) {
        pfa_init(mb,
                 (unsigned int)&kernel_physical_start,
                 (unsigned int)&kernel_physical_end);
    } else {
        log_warning("[kernel_init] bad Multiboot magic 0x%x – skipping PFA",
                    mb_magic);
    }

    /*
     * Initialise the kernel heap.
     * The heap lives entirely within the initial 4 MB PSE mapping, so
     * no additional page tables are needed at this stage.
     */
    kheap_init();

    interrupts_enable();
}
