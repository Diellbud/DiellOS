
BITS 32

SECTION .multiboot
align 4
MB_MAGIC    equ 0x1BADB002
MB_FLAGS    equ 0x00000000
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

dd MB_MAGIC
dd MB_FLAGS
dd MB_CHECKSUM

SECTION .text
global _start
extern kmain

_start:
    mov esp, stack_top
    xor ebp, ebp

    
    
    
    push ebx
    push eax
    call kmain
    add esp, 8

.hang:
    cli
    hlt
    jmp .hang

SECTION .bss
align 16
stack_bottom:
    resb 16384
stack_top:
