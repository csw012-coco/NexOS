bits 32

section .text

global bootx_enter_long_mode

bootx_enter_long_mode:
    mov eax, [esp + 4]
    mov [pml4_ptr], eax

    mov eax, [esp + 8]
    mov [entry_lo], eax

    mov eax, [esp + 12]
    mov [entry_hi], eax

    mov eax, [esp + 16]
    mov [boot_info_ptr], eax

    cli
    lgdt [gdt64_descriptor]

    mov eax, [pml4_ptr]
    mov cr3, eax

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax

    jmp 0x08:long_mode_entry

bits 64
long_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov rsp, 0x0000000000098000

    mov edi, [rel boot_info_ptr]

    mov eax, [rel entry_lo]
    mov edx, [rel entry_hi]
    shl rdx, 32
    or rax, rdx
    jmp rax

section .data
align 8
gdt64:
    dq 0x0000000000000000
    dq 0x00AF9A000000FFFF
    dq 0x00AF92000000FFFF
gdt64_descriptor:
    dw gdt64_descriptor - gdt64 - 1
    dd gdt64

pml4_ptr:      dd 0
entry_lo:      dd 0
entry_hi:      dd 0
boot_info_ptr: dd 0

section .note.GNU-stack noalloc noexec nowrite progbits
