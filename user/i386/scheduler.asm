bits 32

section .text
global _start

_start:
    mov ecx, 0x00300000
.work:
    inc dword [counter]
    dec ecx
    jnz .work

    mov edx, [counter]
    mov eax, 0x53434845
    int 0x40
    ud2

section .data
align 4
counter: dd 0
