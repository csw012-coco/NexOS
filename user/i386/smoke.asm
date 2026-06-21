bits 32

section .text
global _start

_start:
    mov ecx, 0x01000000
.wait_for_timer:
    dec ecx
    jnz .wait_for_timer
    mov eax, 0x4e585533
    mov ebx, 0x52494e47
    int 0x40
    ud2
