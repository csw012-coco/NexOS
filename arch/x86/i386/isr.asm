bits 32

section .text
global i386_isr3
global i386_isr14
global i386_trigger_page_fault
global i386_syscall_stub
global i386_usermode_enter
global i386_usermode_enter_context
global i386_page_fault_test_fault
global i386_page_fault_test_recovery
extern i386_exception_handler
extern i386_irq_handler
extern i386_syscall_handler

%define USER_CS 0x1b
%define USER_DS 0x23

section .bss
align 4
saved_kernel_esp: resd 1

section .text

i386_isr3:
    cld
    push dword 0
    pushad
    mov eax, esp
    push eax
    push dword 3
    call i386_exception_handler
    add esp, 8
    popad
    add esp, 4
    iretd

i386_isr14:
    cld
    pushad
    mov eax, esp
    push eax
    push dword 14
    call i386_exception_handler
    add esp, 8
    popad
    add esp, 4
    iretd

i386_trigger_page_fault:
i386_page_fault_test_fault:
    mov dword [0x02000000], 0x50464f4b
i386_page_fault_test_recovery:
    ret

%macro IRQ_STUB 2
global %1
%1:
    cld
    pushad
    mov eax, esp
    push eax
    push dword %2
    call i386_irq_handler
    add esp, 8
    test eax, eax
    jz %%same_context
    mov esp, eax
%%same_context:
    popad
    iretd
%endmacro

IRQ_STUB i386_irq0, 0
IRQ_STUB i386_irq1, 1
IRQ_STUB i386_irq2, 2
IRQ_STUB i386_irq3, 3
IRQ_STUB i386_irq4, 4
IRQ_STUB i386_irq5, 5
IRQ_STUB i386_irq6, 6
IRQ_STUB i386_irq7, 7
IRQ_STUB i386_irq8, 8
IRQ_STUB i386_irq9, 9
IRQ_STUB i386_irq10, 10
IRQ_STUB i386_irq11, 11
IRQ_STUB i386_irq12, 12
IRQ_STUB i386_irq13, 13
IRQ_STUB i386_irq14, 14
IRQ_STUB i386_irq15, 15

i386_usermode_enter:
    push ebp
    push ebx
    push esi
    push edi
    mov [saved_kernel_esp], esp

    mov eax, [esp + 20]
    mov edx, [esp + 24]
    cli
    mov cx, USER_DS
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx
    push dword USER_DS
    push edx
    pushfd
    pop ecx
    or ecx, 0x200
    push ecx
    push dword USER_CS
    push eax
    iretd

i386_usermode_enter_context:
    push ebp
    push ebx
    push esi
    push edi
    mov [saved_kernel_esp], esp

    mov eax, [esp + 20]
    cli
    mov cx, USER_DS
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx
    mov esp, eax
    popad
    iretd

i386_syscall_stub:
    cld
    pushad
    mov eax, esp
    push eax
    call i386_syscall_handler
    add esp, 4
    cmp eax, 1
    ja .switch_context
    test eax, eax
    jnz .resume_kernel
    popad
    iretd

.switch_context:
    mov esp, eax
    popad
    iretd

.resume_kernel:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov esp, [saved_kernel_esp]
    pop edi
    pop esi
    pop ebx
    pop ebp
    mov eax, 1
    ret
