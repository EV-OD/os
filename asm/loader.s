global loader
global page_directory

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
    ; Save GRUB multiboot registers before paging setup clobbers eax/ebx.
    ;   eax = Multiboot magic number  (should be 0x2BADB002)
    ;   ebx = Physical address of the Multiboot information structure
    mov esi, eax
    mov edi, ebx

    ; -----------------------------------------------------------------
    ; Set up identity paging with 4 MB pages (Intel SDM Vol. 3A §9.2)
    ;
    ; The compile-time page_directory (see .data section below) maps
    ; every 4 MB region of the 32-bit address space to the same physical
    ; range, so virtual == physical for the entire 4 GB space.
    ; This is the simplest correct paging setup and lets the kernel run
    ; at its current 1 MB load address without any address change.
    ; -----------------------------------------------------------------

    ; 1. Load the physical address of the page directory into CR3.
    ;    The lower 12 bits of CR3 are flags; they are zero here because
    ;    page_directory is aligned to a 4 KB boundary.
    mov eax, page_directory
    mov cr3, eax

    ; 2. Set CR4.PSE (bit 4) – Page Size Extensions – to allow 4 MB pages.
    ;    Without this bit the PS flag in PDEs is ignored and each PDE would
    ;    have to point to a 4 KB-granularity page table instead.
    mov eax, cr4
    or  eax, 0x00000010     ; PSE = bit 4
    mov cr4, eax

    ; 3. Set CR0.PG (bit 31) to enable paging.
    ;    Because the page directory uses identity mapping the very next
    ;    instruction fetch (at physical ~0x00100000) is also the correct
    ;    virtual address, so execution continues without a fault.
    mov eax, cr0
    or  eax, 0x80000000     ; PG = bit 31
    mov cr0, eax

    ; -----------------------------------------------------------------
    ; Paging is now active.  Forward multiboot info to kmain.
    ; Calling convention (cdecl): arguments pushed right-to-left.
    ;   kmain(unsigned int eax, unsigned int ebx)
    ; -----------------------------------------------------------------
    push edi                ; arg2 – multiboot info pointer (original ebx)
    push esi                ; arg1 – multiboot magic        (original eax)
    call kmain

.loop:
    jmp .loop

; =========================================================================
; Compile-time identity page directory – 4 MB pages
;
; Entry layout (PS=1, i.e. 4 MB PDE, Intel SDM Vol. 3A Figure 4-4):
;   bits 31-22 : upper 10 bits of the 4 MB-aligned physical frame address
;   bit  7     : PS=1 (use 4 MB pages; requires CR4.PSE)
;   bit  1     : R/W=1 (read-write)
;   bit  0     : P=1   (present)
;   => flags = 0x83
;
; Entry i maps virtual [i*4MB, (i+1)*4MB) to the identical physical range.
; 1024 entries × 4 MB = 4 GB – the entire 32-bit address space is covered.
; =========================================================================
section .data
align 4096
page_directory:
    %assign i 0
    %rep 1024
        dd (i << 22) | 0x83     ; present | writable | 4 MB page
        %assign i i+1
    %endrep

section .note.GNU-stack noalloc noexec nowrite progbits

