bits 32

global _start
extern stage3_main
extern __bss_start
extern __bss_end

section .text
_start:
    cld
    mov al, '3'
    out 0xe9, al
    mov eax, [esp + 4]
    mov ebx, eax
    mov esp, 0x98000

    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    push ebx
    call stage3_main
    add esp, 4

.hang:
    cli
    hlt
    jmp .hang
