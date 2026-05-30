bits 64

global irq0_stub
global irq1_stub
global irq2_stub
global irq3_stub
global irq4_stub
global irq5_stub
global irq6_stub
global irq7_stub
global irq8_stub
global irq9_stub
global irq10_stub
global irq11_stub
global irq12_stub
global irq13_stub
global irq14_stub
global irq15_stub
global syscall_stub
global divide_error_stub
global double_fault_stub
global invalid_opcode_stub
global general_protection_stub
global page_fault_stub

extern irq_dispatch
extern syscall_dispatch
extern usermode_resume_from_syscall
extern kernel_panic_dispatch_exception
extern kernel_prepare_user_frame_return

%macro ENTER_KERNEL_FROM_USER 1
    test byte [rsp + %1], 0x3
    jz %%done
    swapgs
%%done:
%endmacro

%macro LEAVE_KERNEL_TO_USER 1
    test byte [rsp + %1], 0x3
    jz %%done
    swapgs
%%done:
%endmacro

%macro IRQ_STUB 2
%1:
    cld
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
    ENTER_KERNEL_FROM_USER 128
    test byte [rsp + 128], 0x3
    jz %%skip_user_entry
    mov rdi, rsp
    call kernel_prepare_user_frame_return
%%skip_user_entry:
    mov edi, %2
    mov rsi, rsp
    call irq_dispatch
    mov rbx, 0xfffffffffffffff0
    cmp rax, rbx
    je usermode_resume_from_syscall
    test byte [rsp + 128], 0x3
    jz %%skip_user_return
    mov rdi, rsp
    call kernel_prepare_user_frame_return
    test rax, rax
    jz usermode_resume_from_syscall
%%skip_user_return:
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    LEAVE_KERNEL_TO_USER 8
    iretq
%endmacro

section .text
IRQ_STUB irq0_stub, 32
IRQ_STUB irq1_stub, 33
IRQ_STUB irq2_stub, 34
IRQ_STUB irq3_stub, 35
IRQ_STUB irq4_stub, 36
IRQ_STUB irq5_stub, 37
IRQ_STUB irq6_stub, 38
IRQ_STUB irq7_stub, 39
IRQ_STUB irq8_stub, 40
IRQ_STUB irq9_stub, 41
IRQ_STUB irq10_stub, 42
IRQ_STUB irq11_stub, 43
IRQ_STUB irq12_stub, 44
IRQ_STUB irq13_stub, 45
IRQ_STUB irq14_stub, 46
IRQ_STUB irq15_stub, 47

%macro EXC_ERR_STUB 2
%1:
    cld
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
    ENTER_KERNEL_FROM_USER 136
    mov edi, %2
    mov rsi, rsp
    call kernel_panic_dispatch_exception
    mov rbx, 0xfffffffffffffff0
    cmp rax, rbx
    je usermode_resume_from_syscall
.hang:
    cli
    hlt
    jmp .hang
%endmacro

%macro EXC_NOERR_STUB 2
%1:
    cld
    push qword 0
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
    ENTER_KERNEL_FROM_USER 136
    mov edi, %2
    mov rsi, rsp
    call kernel_panic_dispatch_exception
    mov rbx, 0xfffffffffffffff0
    cmp rax, rbx
    je usermode_resume_from_syscall
.hang:
    cli
    hlt
    jmp .hang
%endmacro

EXC_NOERR_STUB divide_error_stub, 0
EXC_NOERR_STUB invalid_opcode_stub, 6

EXC_ERR_STUB double_fault_stub, 8
EXC_ERR_STUB general_protection_stub, 13
EXC_ERR_STUB page_fault_stub, 14

syscall_stub:
    cld
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
    ENTER_KERNEL_FROM_USER 128
    test byte [rsp + 128], 0x3
    jz .skip_user_entry
    mov rdi, rsp
    call kernel_prepare_user_frame_return
    test rax, rax
    jz usermode_resume_from_syscall
.skip_user_entry:
    mov rdi, rsp
    call syscall_dispatch
    mov rbx, 0xfffffffffffffff0
    cmp rax, rbx
    je usermode_resume_from_syscall
    mov [rsp], rax
    mov rdi, rsp
    call kernel_prepare_user_frame_return
    test rax, rax
    jz usermode_resume_from_syscall
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    LEAVE_KERNEL_TO_USER 8
    iretq
