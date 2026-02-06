/* the name of the entry label */
ENTRY(loader)    


SECTIONS 
{
    . = 1M; /* set the starting address to 1MB */
    .text ALIGN (4): 
    {
        *(.text) /* include all .text sections from the input files */
    }
     .rodata ALIGN (4): 
    {
        *(.rodata) /* include all read only data (.rodata) sections from the input files */
    }

    .data ALIGN (4): 
    {
        *(.data) /* include all .data sections from the input files */
    }
    .bss ALIGN (4): 
    {
        *(.common) /* include all common sections from the input files */
        *(.bss) /* include all .bss sections from the input files */
    }
}

