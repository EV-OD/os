global idt_load

; idt_load - Loads the Interrupt Descriptor Table (IDT)
; stack: [esp + 4] address of the idt_ptr struct (6 bytes: limit + base)
;        [esp    ] return address
idt_load:
    mov eax, [esp + 4]   ; load pointer to idt_ptr struct
    lidt [eax]           ; load IDTR with limit and base
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
