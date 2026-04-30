format elf64
entry _start

_start:
    ; SYS_QUERY = 30
    ; SYS_QUERY_MACHINE_INFO = 15
    mov rax, 30
    mov rbx, 15
    xor rcx, rcx
    xor rdx, rdx
    mov rsi, info
    int 0x40

    ; SYS_WRITE = 3, SYS_FD_STDOUT = 1
    mov rax, 3
    mov rbx, 1
    mov rcx, msg
    mov rdx, 15
    xor rsi, rsi
    int 0x40

    ; SYS_EXIT = 0
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rsi, rsi
    int 0x40

msg db "query returned", 10
info:
    dq 0, 0, 0, 0, 0, 0, 0, 0
    dq 0, 0, 0, 0, 0, 0, 0, 0
    dq 0, 0, 0, 0, 0, 0, 0, 0
    dq 0, 0
