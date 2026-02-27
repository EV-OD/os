; prog_b.s – User-mode test program B
; Writes 'B' characters to VGA row 19 in a tight loop.
; Compiled as a flat binary (nasm -f bin).

BITS 32

    call .here
.here:
    pop  ebx                            ; ebx = runtime base address (PIC)

.loop:
    mov  edi, 0xC00B8000 + (19 * 80 * 2)
    mov  ecx, 40
    mov  ah,  0x0B                      ; bright cyan on black
    mov  al,  'B'
.write:
    stosw
    loop .write

    mov  ecx, 0x100000
.spin:
    dec  ecx
    jnz  .spin

    jmp  .loop
