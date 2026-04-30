    .section .text
    .global syscall4_raw
    .type syscall4_raw, @function
syscall4_raw:
    pushq %rbx
    movq %rsi, %r11
    movq %rcx, %r10
    movq %rdi, %rax
    movq %r11, %rbx
    movq %rdx, %rcx
    movq %r10, %rdx
    movq %r8, %rsi
    int $0x40
    popq %rbx
    ret
