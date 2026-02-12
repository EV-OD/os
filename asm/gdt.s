global gdt_load

; gdt_load - Load the GDT and flush all segment registers
;
; After lgdt loads the GDTR, the segment registers still hold the OLD selectors.
; We must explicitly reload every segment register with the new selectors so the
; CPU uses our GDT entries.  cs requires a far jump; data registers use mov.
;
; stack: [esp + 4] address of the gdt_ptr struct (6-byte struct: 2-byte size + 4-byte addr)
;        [esp    ] return address
gdt_load:
    mov eax, [esp + 4]     ; get the address of the gdt_ptr struct
    lgdt [eax]             ; load the GDT from the struct at [eax]

    ; 0x10 = kernel data segment selector
    ;   Binary: 0000 0000 0001 0000
    ;   Bits 15-3 (index) = 2  → GDT entry 2 (data segment)
    ;   Bit  2    (TI)    = 0  → use GDT (not LDT)
    ;   Bits 1-0  (RPL)   = 0  → ring 0 (kernel privilege)
    ;   Offset in GDT = index * 8 = 2 * 8 = 16 = 0x10
    mov ax, 0x10
    mov ds, ax             ; data segment
    mov es, ax             ; extra segment
    mov fs, ax             ; general-purpose segment
    mov gs, ax             ; general-purpose segment
    mov ss, ax             ; stack segment

    ; 0x08 = kernel code segment selector
    ;   Binary: 0000 0000 0000 1000
    ;   Bits 15-3 (index) = 1  → GDT entry 1 (code segment)
    ;   Bit  2    (TI)    = 0  → use GDT (not LDT)
    ;   Bits 1-0  (RPL)   = 0  → ring 0 (kernel privilege)
    ;   Offset in GDT = index * 8 = 1 * 8 = 8 = 0x08
    ;
    ; cs CANNOT be loaded with mov — a far jump is the standard way.
    ; 'jmp selector:offset' sets cs = 0x08 then jumps to .flush_cs.
    jmp 0x08:.flush_cs

.flush_cs:
    ; cs is now 0x08, all other segment registers are 0x10.
    ; The CPU is fully using our new GDT.
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
