#ifndef KERNEL_INIT_H
#define KERNEL_INIT_H

#include "multiboot.h"

/**
 * kernel_init – Initialise all core kernel subsystems.
 *
 * Boot order:
 *   GDT → IDT → ISR stubs → PIC remap → keyboard → serial
 *   → paging verify → PFA (memory map) → kheap → interrupts enable
 *
 * @param mb_magic  Value in EAX at hand-off from GRUB (should be
 *                  MULTIBOOT_BOOTLOADER_MAGIC = 0x2BADB002).
 * @param mb        Virtual pointer to the Multiboot information structure
 *                  forwarded by loader.s (already offset by KERNEL_VIRTUAL_BASE).
 */
void kernel_init(unsigned int mb_magic, multiboot_info_t *mb);

#endif /* KERNEL_INIT_H */
