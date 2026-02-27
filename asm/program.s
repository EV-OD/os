; Simple user program: prints "Hello from userland!" to the screen
; by writing directly to the VGA text framebuffer at 0xB8000.
;
; Because this is a flat binary loaded at a runtime address unknown
; at assemble time, we use call/pop to get our real address (PIC).

BITS 32

    ; Get our runtime base address (position-independent code)
    call .here
.here:
    pop ebx                                 ; ebx = real address of .here
    lea esi, [ebx + (message - .here)]      ; esi = real address of message

    mov edi, 0xC00B8000 + (5 * 80 * 2)     ; VGA framebuffer (higher-half), row 5
    mov ah, 0x0A                            ; light green on black

.loop:
    lodsb                                   ; load next char from [esi]
    test al, al                             ; null terminator?
    jz .done
    stosw                                   ; write char + attribute to VGA
    jmp .loop

.done:
    hlt
    jmp .done

message:
    db "Hello from userland!", 0
