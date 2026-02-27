; prog_a.s – User-mode test program A
; Writes 'A' characters to VGA row 18 in a tight loop.
; Compiled as a flat binary (nasm -f bin):  intended to be loaded as a
; GRUB module and exec'd by the kernel's user-mode launcher.
;
; Memory layout when loaded by the kernel:
;   Virtual 0x00000000: code (this file)
;   Virtual 0xBFFFFFFB: user stack top (grows down)

BITS 32

    call .here
.here:
    pop  ebx                            ; ebx = runtime base address (PIC)

.loop:
    ; Write 'A' + green attr to VGA row 18, cycling through columns 0-39
    mov  edi, 0xC00B8000 + (18 * 80 * 2)
    mov  ecx, 40
    mov  ah,  0x0A                      ; bright green on black
    mov  al,  'A'
.write:
    stosw
    loop .write

    ; Spin a bit (busy wait) so the update rate is visible
    mov  ecx, 0x100000
.spin:
    dec  ecx
    jnz  .spin

    jmp  .loop
