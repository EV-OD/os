global common_isr_stub
extern interrupt_handler


; ISR_NOERR - Macro to define an ISR for a CPU exception that does not push an error code
%macro ISR_NOERR 1
    global isr%1
isr%1:
    push dword 0          ; dummy error code
    push dword %1         ; interrupt number
    jmp common_isr_stub
%endmacro


; ISR_ERR - Macro to define an ISR for a CPU exception that pushes an error code
%macro ISR_ERR 1
    global isr%1
isr%1:
    push dword %1         ; interrupt number
    jmp common_isr_stub
%endmacro

; IRQ - Macro to define an ISR for a hardware interrupt (IRQ)
; args are IRQ number (0-15) and remapped vector (32-47)
%macro IRQ 2
    global irq%1
irq%1:
    push dword 0          ; dummy error code
    push dword %2         ; interrupt number (remapped vector)
    jmp common_isr_stub
%endmacro

section .text

; Common handler shared by all ISRs/IRQs.
; Stack on entry (top first):
;   [dummy/CPU error code]
;   [interrupt number]
;   [eip]
;   [cs]
;   [eflags]
; We push all general registers with pusha, then call the C dispatcher:
;   interrupt_handler(struct cpu_state *cpu, struct stack_state *stack, unsigned int interrupt)
common_isr_stub:
    ; Interrupt Service Routine (ISR) handler epilogue
    ; 
    ; Saves all general purpose registers to create a cpu_state structure,
    ; then calls the interrupt_handler C function with appropriate arguments.
    ;
    ; Stack layout at entry (after interrupt/exception):
    ;   [ESP+36] = interrupt number
    ;   [ESP+32] = error code or 0
    ;   [ESP+28] = return EIP
    ;   [ESP+24] = return CS
    ;   [ESP+20] = EFLAGS
    ;
    ; Execution flow:
    ;   1. Save all GP registers (pusha) - creates cpu_state at ESP
    ;   2. Prepare arguments for interrupt_handler:
    ;      - arg3: pointer to cpu_state (all saved registers)
    ;      - arg2: pointer to stack_state (error code, EIP, CS, EFLAGS)
    ;      - arg1: interrupt number
    ;   3. Call interrupt_handler(int_num, stack_state*, cpu_state*)
    ;   4. Clean up stack and restore registers
    ;   5. Drop error code and interrupt number from stack
    ;   6. Return from interrupt (iret)
    ; specific order: AX/EAX, CX/ECX, DX/EDX, BX/EBX, SP/ESP, BP/EBP, SI/ESI, and DI/EDI
    pusha

    ; eax holds pointer to cpu_state (top of saved regs)
    mov eax, esp
    ; push args: interrupt number, stack_state*, cpu_state*
    push dword [eax + 36]          ; interrupt number
    lea ecx, [eax + 32]            ; stack_state pointer (error code/eip/cs/eflags)
    push ecx
    push eax
    call interrupt_handler
    add esp, 12

    popa
    add esp, 8                     ; drop error code + interrupt number
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

section .note.GNU-stack noalloc noexec nowrite progbits
