global loader
global page_directory

MAGIC_NUMBER    equ 0x1BADB002      ; define the magic number constant
ALIGN_MODULES   equ 0x00000001      ; tell GRUB to align modules

; calculate the checksum (all options + checksum should equal 0)
CHECKSUM        equ -(MAGIC_NUMBER + ALIGN_MODULES)

; Higher-half kernel constants
KERNEL_VIRTUAL_BASE equ 0xC0000000
KERNEL_PAGE_INDEX   equ (KERNEL_VIRTUAL_BASE >> 22)  ; PDE index 768

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
    ; Set up higher-half paging with 4 MB pages (Intel SDM Vol. 3A §9.2)
    ;
    ; Two page-directory entries are populated:
    ;   Entry   0: identity-maps phys [0, 4 MB) → virt [0, 4 MB)
    ;              Required so the next instruction after enabling paging
    ;              can still be fetched (eip is still a physical address).
    ;   Entry 768: maps phys [0, 4 MB) → virt [0xC0000000, 0xC0400000)
    ;              The kernel's higher-half virtual address range.
    ;
    ; After jumping to the higher-half label we remove entry 0.
    ; -----------------------------------------------------------------

    ; 1. Load the PHYSICAL address of the page directory into CR3.
    ;    Symbols are linked at 0xC0100000+, so subtract the virtual base
    ;    to obtain the physical address GRUB can use before paging is on.
    mov eax, (page_directory - KERNEL_VIRTUAL_BASE)
    mov cr3, eax

    ; 2. Set CR4.PSE (bit 4) – Page Size Extensions – to allow 4 MB pages.
    mov eax, cr4
    or  eax, 0x00000010     ; PSE = bit 4
    mov cr4, eax

    ; 3. Set CR0.PG (bit 31) to enable paging.
    ;    Because entry 0 identity-maps the first 4 MB the very next
    ;    instruction fetch (at physical ~0x00100000) succeeds.
    mov eax, cr0
    or  eax, 0x80000000     ; PG = bit 31
    mov cr0, eax

    ; 4. Jump to a higher-half virtual address.
    ;    `higher_half` is linked at 0xC01XXXXX; the far jump makes eip
    ;    point into the 0xC0000000+ range from now on.
    lea ebx, [higher_half]
    jmp ebx

higher_half:
    ; -----------------------------------------------------------------
    ; Now executing in the higher half (eip > 0xC0000000).
    ; Remove the identity mapping for the first 4 MB and flush the TLB
    ; entry so user-mode processes starting at virtual 0 won't collide
    ; with the kernel.
    ; -----------------------------------------------------------------
    mov dword [page_directory + 0], 0   ; clear PDE entry 0
    invlpg [0]                          ; invalidate TLB for virt 0x00000000

    ; Set up the kernel stack (GRUB's stack was in the identity-mapped
    ; region we just unmapped, so we must switch before doing anything).
    mov esp, kernel_stack_top

    ; -----------------------------------------------------------------
    ; Forward multiboot info to kmain.
    ; edi still holds the PHYSICAL address of the multiboot info struct.
    ; Since physical [0, 4 MB) is mapped to virtual [0xC0000000, …),
    ; add the virtual base so C code can dereference the pointer.
    ; -----------------------------------------------------------------
    add edi, KERNEL_VIRTUAL_BASE

    push edi                ; arg2 – multiboot info pointer (virtual)
    push esi                ; arg1 – multiboot magic        (just a number)
    call kmain

.loop:
    jmp .loop

; =========================================================================
; Compile-time page directory – higher-half, 4 MB pages
;
; Only two entries are initially present:
;   [0]   → identity map first 4 MB   (removed after jump to higher_half)
;   [768] → map 0xC0000000 to phys 0  (permanent kernel mapping)
;
; Entry layout (PS=1, i.e. 4 MB PDE, Intel SDM Vol. 3A Figure 4-4):
;   bits 31-22 : upper 10 bits of the 4 MB-aligned physical frame address
;   bit  7     : PS=1 (use 4 MB pages; requires CR4.PSE)
;   bit  1     : R/W=1 (read-write)
;   bit  0     : P=1   (present)
;   => flags for frame 0 = 0x00000083
; =========================================================================
section .data
align 4096
page_directory:
    dd 0x00000083                                   ; entry 0   – identity map [0, 4MB)
    times (KERNEL_PAGE_INDEX - 1)     dd 0          ; entries 1..767 – not present
    dd 0x00000083                                   ; entry 768 – higher-half [0xC0000000, 0xC0400000)
    times (1024 - KERNEL_PAGE_INDEX - 1) dd 0       ; entries 769..1023 – not present

; =========================================================================
; Kernel stack – 16 KB reserved in BSS (grows downward)
; =========================================================================
section .bss
align 16
kernel_stack_bottom:
    resb 16384              ; 16 KB
kernel_stack_top:

section .note.GNU-stack noalloc noexec nowrite progbits

