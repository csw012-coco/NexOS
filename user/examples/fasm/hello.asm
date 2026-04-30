format elf64
entry _start

_start:
    ; SYS_WRITE = 3, SYS_FD_STDOUT = 1
    mov rax, 3
    mov rbx, 1
    mov rcx, msg
    mov rdx, 13
    xor rsi, rsi
    int 0x40

    ; SYS_EXIT = 0
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rsi, rsi
    int 0x40

msg db "hello, NexOS", 10
