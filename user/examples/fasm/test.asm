format elf64
use64
entry start

start:
    mov rax, 3
    mov rbx, 1
    mov rcx, msg
    mov rdx, msg_len
    xor rsi, rsi
    int 0x40

    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rsi, rsi
    int 0x40

msg db 'H', 'e', 'l', 'l', 'o', ',', ' ', 'w', 'o', 'r', 'l', 'd', '!', 10, 0
msg_len equ $ - msg
