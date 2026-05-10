    .section .text
    .global _start
    .type _start, @function
_start:
    xor %ebp, %ebp
    mov %rsp, %rdi
    and $-16, %rsp
    call __libc_start_main
1:
    mov $0, %rax
    xor %rbx, %rbx
    int $0x40
    ud2
