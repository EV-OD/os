/* the name of the entry label */
ENTRY(loader)

KERNEL_VIRTUAL_BASE = 0xC0000000;

PHDRS {
    text PT_LOAD FLAGS(0x5); /* R | X */
    data PT_LOAD FLAGS(0x6); /* R | W */
}

SECTIONS {
    /*
     * Higher-half kernel: relocate all symbols to 3 GB + 1 MB so the
     * linker resolves absolute references against virtual addresses.
     * The AT() directives tell GRUB to load each section at the
     * corresponding physical address (virtual - 0xC0000000 = 1 MB+).
     */
    . = 0xC0100000;

    /* Export the kernel start addresses so C code (and the PFA) can
     * determine which physical frames belong to the kernel image and
     * must not be handed out as free memory. */
    kernel_virtual_start  = .;
    kernel_physical_start = . - KERNEL_VIRTUAL_BASE;

    .text ALIGN(0x1000) : AT(ADDR(.text) - KERNEL_VIRTUAL_BASE) {
        *(.text)
    } :text

    .rodata ALIGN(0x1000) : AT(ADDR(.rodata) - KERNEL_VIRTUAL_BASE) {
        *(.rodata*)
    } :text

    .data ALIGN(0x1000) : AT(ADDR(.data) - KERNEL_VIRTUAL_BASE) {
        *(.data)
    } :data

    .bss ALIGN(0x1000) : AT(ADDR(.bss) - KERNEL_VIRTUAL_BASE) {
        *(COMMON)
        *(.bss)
    } :data

    /* Export the kernel end address so C code can determine the kernel size. */
    kernel_physical_end = . - KERNEL_VIRTUAL_BASE;
    kernel_virtual_end  = .;

    /DISCARD/ : {
        *(.note.gnu.build-id)
        *(.comment)
    }
}

