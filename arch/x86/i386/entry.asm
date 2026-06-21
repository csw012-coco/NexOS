bits 32

section .text
global _start
extern i386_kernel_main
extern __bss_start
extern __bss_end

_start:
    cli
    cld

    ; boot/x enters through a normal 32-bit cdecl call:
    ;   [esp]     return address
    ;   [esp + 4] const struct bootx_boot_info *
    mov esi, [esp + 4]

    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    mov esp, stack_top
    xor ebp, ebp

    push esi
    call i386_kernel_main
    add esp, 4

.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:
