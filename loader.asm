global loader

MAGIC_NUMBER  equ 0x1BADB002
FLAG equ 0x0
CHECKSUM equ -(MAGIC_NUMBER + FLAG)

section .text
align 4
    dd MAGIC_NUMBER
    dd FLAG
    dd CHECKSUM

loader:
    mov eax, 0xCAFEBABE 
.loop:
    jmp .loop

