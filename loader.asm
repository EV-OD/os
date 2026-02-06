global loader

MAGIC_NUMBER  equ 0x1BADB002
FLAG equ 0x0
CHECKSUM equ -(MAGIC_NUMBER + FLAG)

section .multiboot_header
align 4
    dd MAGIC_NUMBER
    dd FLAG
    dd CHECKSUM

section .text
align 4

extern kmain

loader:
    push ebx
    push eax
    call kmain
.loop:
    jmp .loop

