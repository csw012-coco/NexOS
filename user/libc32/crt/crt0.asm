bits 32

section .text
global _start
extern main
extern exit

_start:
    xor ebp, ebp
    call main
    push eax
    call exit

.halt:
    ud2
    jmp .halt
