bits 32

section .text
global _start
extern main
extern exit

_start:
    xor ebp, ebp
    mov eax, [esp]
    lea ebx, [esp + 4]
    lea ecx, [ebx + eax * 4 + 4]
    push ecx
    push ebx
    push eax
    call main
    add esp, 12
    push eax
    call exit

.halt:
    ud2
    jmp .halt
