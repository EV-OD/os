global common_isr_stub
extern interrupt_handler


; ---------------------------------------------------------------------------
; ISR_NOERR – CPU exception that does NOT push an error code.
;
; Push order (matching IRQ layout):
;   push 0          ; dummy error code
;   push int_num    ; interrupt number (on top)
;
; Stack after both pushes (ESP first):
;   ESP+0: int_num   ESP+4: 0 (error)   ESP+8: EIP  ...
; ---------------------------------------------------------------------------
%macro ISR_NOERR 1
    global isr%1
isr%1:
    push dword 0          ; dummy error code (to match IRQ / ISR_ERR layout)
    push dword %1         ; interrupt number (on top)
    jmp common_isr_stub
%endmacro


; ISR_ERR – CPU exception that DOES push an error code.
; CPU already pushed the error code; we just push the interrupt number on top.
%macro ISR_ERR 1
    global isr%1
isr%1:
    push dword %1         ; interrupt number (CPU already pushed error code)
    jmp common_isr_stub
%endmacro

; IRQ – hardware interrupt (IRQ 0-15, remapped to vectors 32-47).
%macro IRQ 2
    global irq%1
irq%1:
    push dword 0          ; dummy error code
    push dword %2         ; interrupt number (remapped vector)
    jmp common_isr_stub
%endmacro

section .text

; ===========================================================================
; common_isr_stub – shared entry point for all ISRs and IRQs.
;
; Stack on entry (top → bottom, i.e. ESP first):
;   [int_num]           pushed by ISR/IRQ macro
;   [error_code / 0]    pushed by ISR/IRQ macro (or CPU for ISR_ERR)
;   [EIP]               pushed by CPU
;   [CS]                pushed by CPU
;   [EFLAGS]            pushed by CPU
;   [user_ESP]          pushed by CPU only if ring 3 → ring 0 transition
;   [user_SS]           pushed by CPU only if ring 3 → ring 0 transition
;
; We save data-segment registers and all GP registers, switch to kernel
; data segments, call the C dispatcher, then restore everything and iret.
;
; C prototype:
;   unsigned int interrupt_handler(struct cpu_state *cpu,
;                                  struct stack_state *stack,
;                                  unsigned int interrupt);
; ===========================================================================
common_isr_stub:
    pusha                           ; save GP regs (EAX..EDI, 32 bytes)

    ; Save data-segment registers (16 bytes)
    push ds
    push es
    push fs
    push gs

    ; Load kernel data segment into all data segment registers so we can
    ; safely access kernel memory from C code.
    mov ax, 0x10                    ; GDT_KERNEL_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; --------------- stack layout after all pushes -------------------------
    ;   ESP+ 0: GS           (segment save, 4 B)
    ;   ESP+ 4: FS
    ;   ESP+ 8: ES
    ;   ESP+12: DS
    ;   ESP+16: EDI          ← start of pusha block (cpu_state*)
    ;   ESP+20: ESI
    ;   ESP+24: EBP
    ;   ESP+28: (orig ESP from pusha – ignored by popa)
    ;   ESP+32: EBX
    ;   ESP+36: EDX
    ;   ESP+40: ECX
    ;   ESP+44: EAX          ← end of pusha block
    ;   ESP+48: int_num
    ;   ESP+52: error_code   ← start of stack_state*
    ;   ESP+56: EIP
    ;   ESP+60: CS
    ;   ESP+64: EFLAGS
    ;   ESP+68: user_ESP     (ring 3 only)
    ;   ESP+72: user_SS      (ring 3 only)
    ; -----------------------------------------------------------------------

    ; Build C call arguments (cdecl: push right-to-left)
    mov eax, esp
    push dword [eax + 48]          ; arg3: interrupt number
    lea ecx, [eax + 52]            ; arg2: stack_state* (error_code → …)
    push ecx
    lea ecx, [eax + 16]            ; arg1: cpu_state*   (EDI → EAX)
    push ecx
    call interrupt_handler
    add esp, 12                     ; pop 3 arguments

    ; interrupt_handler returns new kernel ESP in EAX (0 = no switch).
    ; On context switch, the new stack has the same layout (seg regs + pusha
    ; + int_num + error_code + iret frame), so the restore sequence below
    ; works identically on the new stack.
    test eax, eax
    jz .no_ctx_switch
    mov esp, eax                    ; switch to new process's kernel stack
.no_ctx_switch:

    ; Restore segment registers
    pop gs
    pop fs
    pop es
    pop ds

    popa                            ; restore GP registers
    add esp, 8                      ; drop int_num + error_code
    iret

; CPU exceptions without error code
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_NOERR 9
ISR_NOERR 15
ISR_NOERR 16
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

; CPU exceptions with hardware-pushed error code
ISR_ERR 8
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_ERR 17

; Hardware IRQs (PIC remapped to 0x20 and 0x28)
IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

; Syscall gate – int 0x80 (vector 128).
; Registered with DPL=3 in the IDT so ring-3 code can invoke it.
ISR_NOERR 128

section .note.GNU-stack noalloc noexec nowrite progbits
