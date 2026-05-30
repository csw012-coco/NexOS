bits 64

global usermode_enter
global usermode_resume_frame
global usermode_resume_from_syscall

%define USER_CS 0x1b
%define USER_DS 0x23
%define USERMODE_KERNEL_CONTEXT_SLOTS 8

section .bss
saved_kernel_depth: resq 1
saved_kernel_resume_index: resq 1
saved_kernel_rsp: resq 8
saved_kernel_rip: resq 8
saved_kernel_rbx: resq 8
saved_kernel_rbp: resq 8
saved_kernel_r12: resq 8
saved_kernel_r13: resq 8
saved_kernel_r14: resq 8
saved_kernel_r15: resq 8

section .text

usermode_enter:
    cli
    mov rcx, [rel saved_kernel_depth]
    cmp rcx, USERMODE_KERNEL_CONTEXT_SLOTS
    jae usermode_bad_context_depth
    lea rdx, [rel saved_kernel_rbx]
    mov [rdx + rcx * 8], rbx
    lea rdx, [rel saved_kernel_rbp]
    mov [rdx + rcx * 8], rbp
    lea rdx, [rel saved_kernel_r12]
    mov [rdx + rcx * 8], r12
    lea rdx, [rel saved_kernel_r13]
    mov [rdx + rcx * 8], r13
    lea rdx, [rel saved_kernel_r14]
    mov [rdx + rcx * 8], r14
    lea rdx, [rel saved_kernel_r15]
    mov [rdx + rcx * 8], r15
    lea rdx, [rel saved_kernel_rsp]
    mov [rdx + rcx * 8], rsp
    lea rax, [rel usermode_kernel_resume]
    lea rdx, [rel saved_kernel_rip]
    mov [rdx + rcx * 8], rax
    inc rcx
    mov [rel saved_kernel_depth], rcx

    push qword USER_DS
    push rsi
    pushfq
    pop rax
    or rax, 0x200
    and rax, 0xfffffffffffffbff
    push rax
    push qword USER_CS
    push rdi
    swapgs
    iretq

usermode_resume_frame:
    cli
    mov rcx, [rel saved_kernel_depth]
    cmp rcx, USERMODE_KERNEL_CONTEXT_SLOTS
    jae usermode_bad_context_depth
    lea r8, [rel saved_kernel_rbx]
    mov [r8 + rcx * 8], rbx
    lea r8, [rel saved_kernel_rbp]
    mov [r8 + rcx * 8], rbp
    lea r8, [rel saved_kernel_r12]
    mov [r8 + rcx * 8], r12
    lea r8, [rel saved_kernel_r13]
    mov [r8 + rcx * 8], r13
    lea r8, [rel saved_kernel_r14]
    mov [r8 + rcx * 8], r14
    lea r8, [rel saved_kernel_r15]
    mov [r8 + rcx * 8], r15
    lea r8, [rel saved_kernel_rsp]
    mov [r8 + rcx * 8], rsp
    lea rax, [rel usermode_kernel_resume]
    lea r8, [rel saved_kernel_rip]
    mov [r8 + rcx * 8], rax
    inc rcx
    mov [rel saved_kernel_depth], rcx
    mov rdx, rdi

    push qword [rdx + 152]
    push qword [rdx + 144]
    mov rax, [rdx + 136]
    or rax, 0x200
    and rax, 0xfffffffffffffbff
    push rax
    push qword [rdx + 128]
    push qword [rdx + 120]

    mov r15, [rdx + 112]
    mov r14, [rdx + 104]
    mov r13, [rdx + 96]
    mov r12, [rdx + 88]
    mov r11, [rdx + 80]
    mov r10, [rdx + 72]
    mov r9,  [rdx + 64]
    mov r8,  [rdx + 56]
    mov rbp, [rdx + 48]
    mov rdi, [rdx + 40]
    mov rsi, [rdx + 32]
    mov rcx, [rdx + 16]
    mov rbx, [rdx + 8]
    mov rax, [rdx + 0]
    mov rdx, [rdx + 24]
    swapgs
    iretq

usermode_kernel_resume:
    mov rcx, [rel saved_kernel_resume_index]
    cmp rcx, USERMODE_KERNEL_CONTEXT_SLOTS
    jae usermode_bad_context_depth
    lea rdx, [rel saved_kernel_rbx]
    mov rbx, [rdx + rcx * 8]
    lea rdx, [rel saved_kernel_rbp]
    mov rbp, [rdx + rcx * 8]
    lea rdx, [rel saved_kernel_r12]
    mov r12, [rdx + rcx * 8]
    lea rdx, [rel saved_kernel_r13]
    mov r13, [rdx + rcx * 8]
    lea rdx, [rel saved_kernel_r14]
    mov r14, [rdx + rcx * 8]
    lea rdx, [rel saved_kernel_r15]
    mov r15, [rdx + rcx * 8]
    ret

usermode_resume_from_syscall:
    mov rcx, [rel saved_kernel_depth]
    test rcx, rcx
    jz usermode_bad_context_depth
    dec rcx
    cmp rcx, USERMODE_KERNEL_CONTEXT_SLOTS
    jae usermode_bad_context_depth
    mov [rel saved_kernel_depth], rcx
    mov [rel saved_kernel_resume_index], rcx
    lea rdx, [rel saved_kernel_rsp]
    mov rsp, [rdx + rcx * 8]
    lea rdx, [rel saved_kernel_rip]
    mov rax, [rdx + rcx * 8]
    jmp rax

usermode_bad_context_depth:
    cli
.halt:
    hlt
    jmp .halt
