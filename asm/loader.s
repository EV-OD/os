global loader

MAGIC_NUMBER    equ 0x1BADB002      ; define the magic number constant
ALIGN_MODULES   equ 0x00000001      ; tell GRUB to align modules

; calculate the checksum (all options + checksum should equal 0)
CHECKSUM        equ -(MAGIC_NUMBER + ALIGN_MODULES)

section .text                        ; start of the text (code) section
align 4                              ; the code must be 4 byte aligned
    dd MAGIC_NUMBER                  ; write the magic number
    dd ALIGN_MODULES                 ; write the align modules instruction
    dd CHECKSUM                      ; write the checksum



extern kmain

loader:
    push ebx
    push eax
    call kmain

.loop:
    jmp .loop

section .note.GNU-stack noalloc noexec nowrite progbits

