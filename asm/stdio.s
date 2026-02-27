global outb             ; make the label outb visible outside this file

; outb - send a byte to an I/O port
; stack: [esp + 8] the data byte
;        [esp + 4] the I/O port
;        [esp    ] return address
outb:
    mov al, [esp + 8]    ; move the data to be sent into the al register
    mov dx, [esp + 4]    ; move the address of the I/O port into the dx register
    out dx, al           ; send the data to the I/O port
    ret                  ; return to the calling function


global inb

; inb - returns a byte from the given I/O port
; stack: [esp + 4] The address of the I/O port
;        [esp    ] The return address
inb:
    mov dx, [esp + 4]       ; move the address of the I/O port to the dx register
    in  al, dx              ; read a byte from the I/O port and store it in the al register
    ret

global outw

; outw - send a 16-bit word to an I/O port
; stack: [esp + 8] the data word
;        [esp + 4] the I/O port
;        [esp    ] return address
outw:
    mov ax, [esp + 8]       ; move the 16-bit data into ax
    mov dx, [esp + 4]       ; move the I/O port address into dx
    out dx, ax              ; send the word to the I/O port
    ret

global inw

; inw - returns a 16-bit word from the given I/O port
; stack: [esp + 4] The address of the I/O port
;        [esp    ] The return address
inw:
    mov dx, [esp + 4]       ; move the address of the I/O port to dx
    in  ax, dx              ; read a 16-bit word from the I/O port into ax
    ret

section .note.GNU-stack noalloc noexec nowrite progbits