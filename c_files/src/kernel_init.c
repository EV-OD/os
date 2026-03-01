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
#include "tss.h"
#include "pit.h"
#include "log.h"
#include "vfs.h"
#include "syscall.h"
#include "process.h"
#include "sched.h"

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
        /*
         * Reserve the ENTIRE kernel heap virtual address range from the PFA.
         *
         * The kheap spans virtual [kernel_virtual_end, KHEAP_VEND) which
         * corresponds to physical [~0xaf6000, 0x3000000).  The PFA must not
         * hand out any frame in that physical range to user processes.
         *
         * Reserving only (kernel_physical_end + KHEAP_INITIAL_SIZE) was not
         * enough: kheap_expand() grows the heap beyond the initial 4 MB on
         * demand (2 MB at a time).  If the PFA has already given a frame
         * inside the expansion range to a user page directory, kheap_expand
         * writes a block header on top of that page directory, corrupting it.
         * When the scheduler reloads CR3 the CPU triple-faults and reboots.
         *
         * Reserving up to KHEAP_VEND (48 MB physical) is safe: the system
         * has 64 MB of RAM, leaving 16 MB of physical frames (above 48 MB)
         * available to user processes – more than sufficient for demos.
         */
        pfa_init(mb,
                 (unsigned int)&kernel_physical_start,
                 KHEAP_VEND - 0xC0000000u);   /* 0x3000000 = 48 MB */

        /*
         * Extend kernel page-directory mapping so PHYS_TO_VIRT() works for
         * ALL physical frames returned by the PFA, not just the first 4 MB.
         * mem_upper is in KiB starting from 1 MB; add the first 1 MB back.
         */
        if (mb->flags & MULTIBOOT_INFO_MEMORY) {
            unsigned int total_ram = ((unsigned int)mb->mem_upper + 1024u) * 1024u;
            paging_map_full_kernel_ram(total_ram);
        }
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

    /*
     * Initialise the virtual filesystem (ATA + FAT32).
     * Must come after kheap_init() (uses kmalloc internally).
     * Probes the primary ATA drive, locates the FAT partition in the MBR,
     * and mounts it.  If no drive or no FAT partition is found the system
     * continues without a mounted filesystem.
     */
    if (fs_init() < 0) {
        log_warning("[kernel_init] Filesystem not available");
    }

    /*
     * Install the TSS descriptor into GDT[5] and load the Task Register.
     * Must come after gdt_init() (GDT is valid) and kheap_init() (not
     * strictly required, but ordering is conventional).
     */
    tss_init();

    /*
     * Configure the PIT channel 0 for PIT_TICK_MS millisecond intervals.
     * This drives the CFS scheduler's preemptive tick.
     * Ensure IRQ0 is unmasked so timer interrupts actually arrive.
     */
    pit_init(PIT_TICK_MS);
    pic_clear_mask(0);  /* unmask IRQ0 (PIT) */

    /*
     * Initialise the syscall interface (int 0x80 handler).
     * Must come after isr_install() registered the ISR stub.
     */
    syscall_init();

    /*
     * Initialise the process and scheduler subsystems.
     * Must come before any process_create*() calls.
     */
    process_init();
    sched_init();

    interrupts_enable();
}
