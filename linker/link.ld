/* the name of the entry label */
ENTRY(loader)

PHDRS {
    text PT_LOAD FLAGS(0x5); /* R | X */
    data PT_LOAD FLAGS(0x6); /* R | W */
}

SECTIONS {
    . = 1M; /* set the starting address to 1MB */

    .text ALIGN(4) : {
        *(.text)
    } :text

    .rodata ALIGN(4) : {
        *(.rodata)
    } :text

    .data ALIGN(4) : {
        *(.data)
    } :data

    .bss ALIGN(4) : {
        *(.common)
        *(.bss)
    } :data

    /DISCARD/ : {
        *(.note.gnu.build-id)
        *(.comment)
    }
}

