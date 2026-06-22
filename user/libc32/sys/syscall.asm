bits 32

section .text
global __nlibc32_syscall4

__nlibc32_syscall4:
    push ebx
    push esi
    push edi
    push ebp

    mov eax, [esp + 20]
    mov ebx, [esp + 24]
    mov ecx, [esp + 28]
    mov edx, [esp + 32]
    mov esi, [esp + 36]
    int 0x40

    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
