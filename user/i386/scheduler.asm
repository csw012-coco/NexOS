bits 32

section .text
global _start

_start:
    mov eax, 15
    int 0x40
    mov ebp, eax

    mov eax, 16
    int 0x40

    mov ecx, 0x00300000
.work:
    inc dword [counter]
    dec ecx
    jnz .work

    mov edx, [counter]
    mov eax, 0
    mov ebx, ebp
    int 0x40
    ud2

section .data
align 4
counter: dd 0
