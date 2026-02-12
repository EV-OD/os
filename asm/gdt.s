global gdt_load

; gdt_load - Load the GDT and flush all segment registers
; stack: [esp + 4] address of the gdt_ptr struct
;        [esp    ] return address
gdt_load:
    mov eax, [esp + 4]     ; get the address of the gdt_ptr struct
    lgdt [eax]             ; load the GDT

    ; Load 0x10 (kernel data segment, index 2) into all data segment registers
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Far jump to load 0x08 (kernel code segment, index 1) into cs
    jmp 0x08:.flush_cs

.flush_cs:
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
